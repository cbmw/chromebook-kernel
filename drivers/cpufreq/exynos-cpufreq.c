/*
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS - CPU frequency scaling support for EXYNOS series
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/cpufreq.h>
#include <linux/suspend.h>

#include <mach/cpufreq.h>

#include <plat/cpu.h>

static struct exynos_dvfs_info *exynos_info;

static struct regulator *arm_regulator;
static struct cpufreq_freqs freqs;

static unsigned int locking_frequency;
static bool frequency_locked;
static DEFINE_MUTEX(cpufreq_lock);

static int exynos_verify_speed(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy,
					      exynos_info->freq_table);
}

static unsigned int exynos_getspeed(unsigned int cpu)
{
	return clk_get_rate(exynos_info->cpu_clk) / 1000;
}

static int exynos_target(struct cpufreq_policy *policy,
			  unsigned int target_freq,
			  unsigned int relation)
{
	unsigned int index, old_index;
	unsigned int arm_volt, safe_arm_volt = 0;
	int ret = 0;
	unsigned int saved_min = 0, saved_max = 0;
	struct cpufreq_frequency_table *freq_table = exynos_info->freq_table;
	unsigned int *volt_table = exynos_info->volt_table;
	unsigned int mpll_freq_khz = exynos_info->mpll_freq_khz;

	mutex_lock(&cpufreq_lock);

	freqs.old = policy->cur;

	if (frequency_locked && target_freq != locking_frequency) {
		ret = -EAGAIN;
		goto out;
	}

	/*
	 * The policy max have been changed so that we cannot get proper
	 * old_index with cpufreq_frequency_table_target(). Thus, ignore
	 * policy and get the index from the raw freqeuncy table.
	 */
	for (old_index = 0;
		freq_table[old_index].frequency != CPUFREQ_TABLE_END;
		old_index++)
		if (freq_table[old_index].frequency == freqs.old)
			break;

	if (freq_table[old_index].frequency == CPUFREQ_TABLE_END) {
		ret = -EINVAL;
		goto out;
	}


	/*
	 * if we need a specific frequency for suspend/resume,
	 * ensure we can set it whatever the governor/userspace is currently
	 * doing.
	 */
	if (frequency_locked) {
		saved_min = policy->min;
		saved_max = policy->max;
		policy->min = policy->cpuinfo.min_freq;
		policy->max = policy->cpuinfo.max_freq;
	}

	if (cpufreq_frequency_table_target(policy, freq_table,
					   target_freq, relation, &index)) {
		ret = -EINVAL;
		goto out;
	}

	freqs.new = freq_table[index].frequency;
	freqs.cpu = policy->cpu;

	/*
	 * restore the policy frequency settings,
	 * if we force it due to frequency locking.
	 */
	if (saved_min || saved_max) {
		policy->min = saved_min;
		policy->max = saved_max;
	}


	/*
	 * ARM clock source will be changed APLL to MPLL temporary
	 * To support this level, need to control regulator for
	 * required voltage level
	 */
	if (exynos_info->need_apll_change != NULL) {
		if (exynos_info->need_apll_change(old_index, index) &&
		   (freq_table[index].frequency < mpll_freq_khz) &&
		   (freq_table[old_index].frequency < mpll_freq_khz))
			safe_arm_volt = volt_table[exynos_info->pll_safe_idx];
	}
	arm_volt = volt_table[index];

	for_each_cpu(freqs.cpu, policy->cpus)
		cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	/* When the new frequency is higher than current frequency */
	if ((freqs.new > freqs.old) && !safe_arm_volt) {
		/* Firstly, voltage up to increase frequency */
		regulator_set_voltage(arm_regulator, arm_volt,
				arm_volt);
	}

	if (safe_arm_volt)
		regulator_set_voltage(arm_regulator, safe_arm_volt,
				      safe_arm_volt);
	if (freqs.new != freqs.old)
		exynos_info->set_freq(old_index, index);

	for_each_cpu(freqs.cpu, policy->cpus)
		cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	/* When the new frequency is lower than current frequency */
	if ((freqs.new < freqs.old) ||
	   ((freqs.new > freqs.old) && safe_arm_volt)) {
		/* down the voltage after frequency change */
		regulator_set_voltage(arm_regulator, arm_volt,
				arm_volt);
	}

out:
	mutex_unlock(&cpufreq_lock);

	return ret;
}

#ifdef CONFIG_PM
static int exynos_cpufreq_suspend(struct cpufreq_policy *policy)
{
	return 0;
}

static int exynos_cpufreq_resume(struct cpufreq_policy *policy)
{
	return 0;
}
#endif

/**
 * exynos_cpufreq_pm_notifier - block CPUFREQ's activities in suspend-resume
 *			context
 * @notifier
 * @pm_event
 * @v
 *
 * While frequency_locked == true, target() ignores every frequency but
 * locking_frequency. The locking_frequency value is the maximum CPU frequency,
 * to ensure with the highest core voltage. In order to eliminate possible
 * inconsistency in clock values, we save and restore frequencies during
 * suspend and resume and block CPUFREQ activities. Note that the standard
 * suspend/resume cannot be used as they are too deep (syscore_ops) for
 * regulator actions.
 */
static int exynos_cpufreq_pm_notifier(struct notifier_block *notifier,
				       unsigned long pm_event, void *v)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(0); /* boot CPU */
	static unsigned int saved_frequency;
	unsigned int temp;

	mutex_lock(&cpufreq_lock);
	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		if (frequency_locked)
			goto out;

		frequency_locked = true;

		if (locking_frequency) {
			saved_frequency = exynos_getspeed(0);

			mutex_unlock(&cpufreq_lock);
			exynos_target(policy, locking_frequency,
				      CPUFREQ_RELATION_H);
			mutex_lock(&cpufreq_lock);
		}
		break;

	case PM_POST_SUSPEND:
		if (saved_frequency) {
			/*
			 * While frequency_locked, only locking_frequency
			 * is valid for target(). In order to use
			 * saved_frequency while keeping frequency_locked,
			 * we temporarly overwrite locking_frequency.
			 */
			temp = locking_frequency;
			locking_frequency = saved_frequency;

			mutex_unlock(&cpufreq_lock);
			exynos_target(policy, locking_frequency,
				      CPUFREQ_RELATION_H);
			mutex_lock(&cpufreq_lock);

			locking_frequency = temp;
		}
		frequency_locked = false;
		break;
	}
out:
	mutex_unlock(&cpufreq_lock);

	return NOTIFY_OK;
}

static struct notifier_block exynos_cpufreq_nb = {
	.notifier_call = exynos_cpufreq_pm_notifier,
};

static int exynos_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	int ret;
	u32 cap_max;
	const struct device_node *np = NULL;
	const char *cpu_dt_path;

	policy->cur = policy->min = policy->max = exynos_getspeed(policy->cpu);

	cpufreq_frequency_table_get_attr(exynos_info->freq_table, policy->cpu);

	/* set the transition latency value */
	policy->cpuinfo.transition_latency = 100000;

	/*
	 * EXYNOS4 multi-core processors has 2 cores
	 * that the frequency cannot be set independently.
	 * Each cpu is bound to the same speed.
	 * So the affected cpu is all of the cpus.
	 */
	if (num_online_cpus() == 1) {
		cpumask_copy(policy->related_cpus, cpu_possible_mask);
		cpumask_copy(policy->cpus, cpu_online_mask);
	} else {
		policy->shared_type = CPUFREQ_SHARED_TYPE_ANY;
		cpumask_setall(policy->cpus);
	}

	ret = cpufreq_frequency_table_cpuinfo(policy, exynos_info->freq_table);
	if (ret)
		return ret;

	/*
	 * If the CPU node in the device tree has a clock frequency set,
	 * this means our firmware wants us to cap the CPU frequency.
	 * We are set the current max frequency to that value,
	 * but this might be overriden in the userspace.
	 */
	cpu_dt_path = kasprintf(GFP_KERNEL, "/cpus/cpu@%d", policy->cpu);
	if (cpu_dt_path) {
		np = of_find_node_by_path(cpu_dt_path);
		kfree(cpu_dt_path);
	}
	if (np && !of_property_read_u32(np, "clock-frequency-limit",
					&cap_max)) {
		pr_info("Capping CPU%d frequency to %d Mhz\n",
			policy->cpu, cap_max / 1000);
		policy->max = min(policy->max, cap_max);
	} else {
		pr_info("NOT Capping CPU%d frequency\n", policy->cpu);
	}

	return 0;
}

static int exynos_cpufreq_cpu_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_put_attr(policy->cpu);
	return 0;
}

static struct freq_attr *exynos_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver exynos_driver = {
	.flags		= CPUFREQ_STICKY,
	.verify		= exynos_verify_speed,
	.target		= exynos_target,
	.get		= exynos_getspeed,
	.init		= exynos_cpufreq_cpu_init,
	.exit		= exynos_cpufreq_cpu_exit,
	.name		= "exynos_cpufreq",
	.attr		= exynos_cpufreq_attr,
#ifdef CONFIG_PM
	.suspend	= exynos_cpufreq_suspend,
	.resume		= exynos_cpufreq_resume,
#endif
};

static int __init exynos_cpufreq_init(void)
{
	int ret = -EINVAL;

#ifdef CONFIG_ARM_EXYNOS_IKS_CORE
	if (soc_is_exynos542x())
		return exynos_iks_cpufreq_init();
#endif

	exynos_info = kzalloc(sizeof(struct exynos_dvfs_info), GFP_KERNEL);
	if (!exynos_info)
		return -ENOMEM;

	if (soc_is_exynos4210())
		ret = exynos4210_cpufreq_init(exynos_info);
	else if (soc_is_exynos4212() || soc_is_exynos4412())
		ret = exynos4x12_cpufreq_init(exynos_info);
	else if (soc_is_exynos5250())
		ret = exynos5250_cpufreq_init(exynos_info);
	else if (soc_is_exynos542x())
		ret = exynos5420_cpufreq_init(exynos_info);
	else
		pr_err("%s: CPU type not found\n", __func__);

	if (ret)
		goto err_vdd_arm;

	if (exynos_info->set_freq == NULL) {
		pr_err("%s: No set_freq function (ERR)\n", __func__);
		goto err_vdd_arm;
	}

	arm_regulator = regulator_get(NULL, "vdd_arm");
	if (IS_ERR(arm_regulator)) {
		pr_err("%s: failed to get resource vdd_arm\n", __func__);
		goto err_vdd_arm;
	}

	locking_frequency =
		exynos_info->freq_table[exynos_info->max_support_idx].frequency;

	register_pm_notifier(&exynos_cpufreq_nb);

	if (cpufreq_register_driver(&exynos_driver)) {
		pr_err("%s: failed to register cpufreq driver\n", __func__);
		goto err_cpufreq;
	}

	return 0;
err_cpufreq:
	unregister_pm_notifier(&exynos_cpufreq_nb);

	if (!IS_ERR(arm_regulator))
		regulator_put(arm_regulator);
err_vdd_arm:
	kfree(exynos_info);
	pr_debug("%s: failed initialization\n", __func__);
	return -EINVAL;
}
late_initcall(exynos_cpufreq_init);
