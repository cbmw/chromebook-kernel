/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2011-2012 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file mali_kbase_pm_metrics.c
 * Metrics for power management
 */

#include <osk/mali_osk.h>

#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_kbase_pm.h>

/* When VSync is being hit aim for utilisation between 70-90% */
#define KBASE_PM_VSYNC_MIN_UTILISATION          70
#define KBASE_PM_VSYNC_MAX_UTILISATION          90
/* Otherwise aim for 10-40% */
#define KBASE_PM_NO_VSYNC_MIN_UTILISATION       10
#define KBASE_PM_NO_VSYNC_MAX_UTILISATION       40

static void dvfs_callback(void *data)
{
	kbase_device *kbdev;
	osk_error ret;

	OSK_ASSERT(data != NULL);

	kbdev = (kbase_device*)data;
	kbase_platform_dvfs_event(kbdev);

	osk_spinlock_irq_lock(&kbdev->pm.metrics.lock);
	if (kbdev->pm.metrics.timer_active)
	{
		ret = osk_timer_start(&kbdev->pm.metrics.timer, KBASE_PM_DVFS_FREQUENCY);
		if (ret != OSK_ERR_NONE)
		{
			/* Handle the situation where the timer cannot be restarted */
		}
	}
	osk_spinlock_irq_unlock(&kbdev->pm.metrics.lock);
}

mali_error kbasep_pm_metrics_init(kbase_device *kbdev)
{
	osk_error osk_err;
	mali_error ret;

	OSK_ASSERT(kbdev != NULL);

	kbdev->pm.metrics.vsync_hit = 0;
	kbdev->pm.metrics.utilisation = 0;

	kbdev->pm.metrics.time_period_start = osk_time_now();
	kbdev->pm.metrics.time_busy = 0;
	kbdev->pm.metrics.time_idle = 0;
	kbdev->pm.metrics.gpu_active = MALI_TRUE;
	kbdev->pm.metrics.timer_active = MALI_TRUE;

	osk_err = osk_spinlock_irq_init(&kbdev->pm.metrics.lock, OSK_LOCK_ORDER_PM_METRICS);
	if (OSK_ERR_NONE != osk_err)
	{
		ret = MALI_ERROR_FUNCTION_FAILED;
		goto out;
	}

	osk_err = osk_timer_init(&kbdev->pm.metrics.timer);
	if (OSK_ERR_NONE != osk_err)
	{
		ret = MALI_ERROR_FUNCTION_FAILED;
		goto spinlock_free;
	}
	osk_timer_callback_set(&kbdev->pm.metrics.timer, dvfs_callback, kbdev);
	osk_err = osk_timer_start(&kbdev->pm.metrics.timer, KBASE_PM_DVFS_FREQUENCY);
	if (OSK_ERR_NONE != osk_err)
	{
		ret = MALI_ERROR_FUNCTION_FAILED;
		goto timer_free;
	}

	kbase_pm_register_vsync_callback(kbdev);
	ret = MALI_ERROR_NONE;
	goto out;

timer_free:
	osk_timer_stop(&kbdev->pm.metrics.timer);
	osk_timer_term(&kbdev->pm.metrics.timer);
spinlock_free:
	osk_spinlock_irq_term(&kbdev->pm.metrics.lock);
out:
	return ret;
}
KBASE_EXPORT_TEST_API(kbasep_pm_metrics_init)

void kbasep_pm_metrics_term(kbase_device *kbdev)
{
	OSK_ASSERT(kbdev != NULL);

	osk_spinlock_irq_lock(&kbdev->pm.metrics.lock);
	kbdev->pm.metrics.timer_active = MALI_FALSE;
	osk_spinlock_irq_unlock(&kbdev->pm.metrics.lock);

	osk_timer_stop(&kbdev->pm.metrics.timer);
	osk_timer_term(&kbdev->pm.metrics.timer);

	kbase_pm_unregister_vsync_callback(kbdev);

	osk_spinlock_irq_term(&kbdev->pm.metrics.lock);
}
KBASE_EXPORT_TEST_API(kbasep_pm_metrics_term)

mali_bool kbasep_pm_metrics_isactive(kbase_device *kbdev)
{
	mali_bool isactive;

	OSK_ASSERT(kbdev != NULL);

	osk_spinlock_irq_lock(&kbdev->pm.metrics.lock);
	isactive = (kbdev->pm.metrics.timer_active == MALI_TRUE);
	osk_spinlock_irq_unlock(&kbdev->pm.metrics.lock);

	return isactive;
}
KBASE_EXPORT_TEST_API(kbasep_pm_metrics_isactive)

void kbasep_pm_record_gpu_idle(kbase_device *kbdev)
{
	osk_ticks now = osk_time_now();

	OSK_ASSERT(kbdev != NULL);

	osk_spinlock_irq_lock(&kbdev->pm.metrics.lock);

	OSK_ASSERT(kbdev->pm.metrics.gpu_active == MALI_TRUE);

	kbdev->pm.metrics.gpu_active = MALI_FALSE;

	kbdev->pm.metrics.time_busy += osk_time_elapsed(kbdev->pm.metrics.time_period_start, now);
	kbdev->pm.metrics.time_period_start = now;

	osk_spinlock_irq_unlock(&kbdev->pm.metrics.lock);
}
KBASE_EXPORT_TEST_API(kbasep_pm_record_gpu_idle)

void kbasep_pm_record_gpu_active(kbase_device *kbdev)
{
	osk_ticks now = osk_time_now();

	OSK_ASSERT(kbdev != NULL);

	osk_spinlock_irq_lock(&kbdev->pm.metrics.lock);

	OSK_ASSERT(kbdev->pm.metrics.gpu_active == MALI_FALSE);

	kbdev->pm.metrics.gpu_active = MALI_TRUE;

	kbdev->pm.metrics.time_idle += osk_time_elapsed(kbdev->pm.metrics.time_period_start, now);
	kbdev->pm.metrics.time_period_start = now;

	osk_spinlock_irq_unlock(&kbdev->pm.metrics.lock);
}
KBASE_EXPORT_TEST_API(kbasep_pm_record_gpu_active)

void kbase_pm_report_vsync(kbase_device *kbdev, int buffer_updated)
{
	OSK_ASSERT(kbdev != NULL);

	osk_spinlock_irq_lock(&kbdev->pm.metrics.lock);
	kbdev->pm.metrics.vsync_hit = buffer_updated;
	osk_spinlock_irq_unlock(&kbdev->pm.metrics.lock);
}
KBASE_EXPORT_TEST_API(kbase_pm_report_vsync)