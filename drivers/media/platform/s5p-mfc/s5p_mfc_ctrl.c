/*
 * linux/drivers/media/platform/s5p-mfc/s5p_mfc_ctrl.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include "s5p_mfc_cmd.h"
#include "s5p_mfc_common.h"
#include "s5p_mfc_debug.h"
#include "s5p_mfc_intr.h"
#include "s5p_mfc_opr.h"
#include "s5p_mfc_pm.h"
#include "s5p_mfc_ctrl.h"

/* Allocate memory for firmware */
int s5p_mfc_alloc_firmware(struct s5p_mfc_dev *dev)
{
	void *bank2_virt;
	dma_addr_t bank2_dma_addr;

	dev->fw_size = dev->variant->buf_size->fw;

	if (dev->fw_virt_addr) {
		mfc_err("Attempting to allocate firmware when it seems that it is already loaded\n");
		return -ENOMEM;
	}

	dev->fw_virt_addr = dma_alloc_coherent(dev->mem_dev_l, dev->fw_size,
					&dev->bank1, GFP_KERNEL);

	if (IS_ERR_OR_NULL(dev->fw_virt_addr)) {
		dev->fw_virt_addr = NULL;
		mfc_err("Allocating bitprocessor buffer failed\n");
		return -ENOMEM;
	}

	dev->bank1 = dev->bank1;

	if (HAS_PORTNUM(dev) && IS_TWOPORT(dev)) {
		bank2_virt = dma_alloc_coherent(dev->mem_dev_r, 1 << MFC_BASE_ALIGN_ORDER,
					&bank2_dma_addr, GFP_KERNEL);

		if (IS_ERR(dev->fw_virt_addr)) {
			mfc_err("Allocating bank2 base failed\n");
			dma_free_coherent(dev->mem_dev_l, dev->fw_size,
				dev->fw_virt_addr, dev->bank1);
			dev->fw_virt_addr = NULL;
			return -ENOMEM;
		}

		/* Valid buffers passed to MFC encoder with LAST_FRAME command
		 * should not have address of bank2 - MFC will treat it as a null frame.
		 * To avoid such situation we set bank2 address below the pool address.
		 */
		dev->bank2 = bank2_dma_addr - (1 << MFC_BASE_ALIGN_ORDER);

		dma_free_coherent(dev->mem_dev_r, 1 << MFC_BASE_ALIGN_ORDER,
			bank2_virt, bank2_dma_addr);

	} else {
		/* In this case bank2 can point to the same address as bank1.
		 * Firmware will always occupy the beggining of this area so it is
		 * impossible having a video frame buffer with zero address. */
		dev->bank2 = dev->bank1;
	}
	return 0;
}

/* Load firmware */
int s5p_mfc_load_firmware(struct s5p_mfc_dev *dev)
{
	struct firmware *fw_blob;
	int err;

	/* Firmare has to be present as a separate file or compiled
	 * into kernel. */
	mfc_debug_enter();

	err = request_firmware((const struct firmware **)&fw_blob,
				     dev->variant->fw_name, dev->v4l2_dev.dev);
	if (err != 0) {
		mfc_err("Firmware is not present in the /lib/firmware directory nor compiled in kernel\n");
		return -EINVAL;
	}
	if (fw_blob->size > dev->fw_size) {
		mfc_err("MFC firmware is too big to be loaded\n");
		release_firmware(fw_blob);
		return -ENOMEM;
	}
	if (!dev->fw_virt_addr) {
		mfc_err("MFC firmware is not allocated\n");
		release_firmware(fw_blob);
		return -EINVAL;
	}
	memcpy(dev->fw_virt_addr, fw_blob->data, fw_blob->size);
	wmb();
	release_firmware(fw_blob);
	mfc_debug_leave();
	return 0;
}

/* Release firmware memory */
int s5p_mfc_release_firmware(struct s5p_mfc_dev *dev)
{
	/* Before calling this function one has to make sure
	 * that MFC is no longer processing */
	if (!dev->fw_virt_addr)
		return -EINVAL;
	dma_free_coherent(dev->mem_dev_l, dev->fw_size, dev->fw_virt_addr,
						dev->bank1);
	dev->fw_virt_addr = NULL;
	return 0;
}

int s5p_mfc_bus_reset(struct s5p_mfc_dev *dev)
{
	unsigned int status;
	unsigned long timeout;

	/* Reset */
	mfc_write(dev, 0x1, S5P_FIMV_MFC_BUS_RESET_CTRL);
	timeout = jiffies + msecs_to_jiffies(MFC_BW_TIMEOUT_MS);
	/* Check bus status */
	do {
		if (time_after(jiffies, timeout)) {
			mfc_err("Timeout while resetting MFC.\n");
			return -EIO;
		}
		status = mfc_read(dev, S5P_FIMV_MFC_BUS_RESET_CTRL);
	} while ((status & 0x2) == 0);
	return 0;
}

int s5p_mfc_init_fw(struct s5p_mfc_dev *dev)
{
	int ret;

	mfc_debug(2, "Will now wait for completion of firmware transfer\n");
	if (s5p_mfc_wait_for_done_dev(dev)) {
		mfc_err("Failed to load firmware\n");
		s5p_mfc_ctrl_ops_call(dev, reset, dev);
		return -EIO;
	}
	/* Lock the HW before issuing sys_init command */
	WARN_ON(s5p_mfc_hw_trylock(dev));
	/* 4. Initialize firmware */
	ret = s5p_mfc_hw_call(dev->mfc_cmds, sys_init_cmd, dev);
	if (ret) {
		mfc_err("Failed to send command to MFC - timeout\n");
		s5p_mfc_ctrl_ops_call(dev, reset, dev);
		return ret;
	}
	mfc_debug(2, "Ok, now will write a command to init the system\n");
	if (s5p_mfc_wait_for_done_dev(dev)) {
		mfc_err("Failed to load firmware\n");
		s5p_mfc_ctrl_ops_call(dev, reset, dev);
		return -EIO;
	}
	return ret;
}

/* Deinitialize hardware */
void s5p_mfc_deinit_hw(struct s5p_mfc_dev *dev)
{
	s5p_mfc_clock_on(dev);

	s5p_mfc_ctrl_ops_call(dev, reset, dev);
	s5p_mfc_hw_call(dev->mfc_ops, release_dev_context_buffer, dev);

	dev->risc_on = 0;
	s5p_mfc_clock_off(dev);

	/* Hardware will start in a clean state on next init. */
	clear_bit(0, &dev->hw_error);
}

int s5p_mfc_sleep(struct s5p_mfc_dev *dev)
{
	int ret;

	mfc_debug_enter();
	s5p_mfc_clock_on(dev);
	/* Lock the HW before issuing sleep command */
	WARN_ON(s5p_mfc_hw_trylock(dev));
	ret = s5p_mfc_hw_call(dev->mfc_cmds, sleep_cmd, dev);
	if (ret) {
		mfc_err("Failed to send command to MFC - timeout\n");
		return ret;
	}
	if (s5p_mfc_wait_for_done_dev(dev)) {
		mfc_err("Failed to sleep\n");
		return -EIO;
	}
	s5p_mfc_clock_off(dev);
	dev->risc_on = 0;
	mfc_debug_leave();
	return ret;
}

int s5p_mfc_open_mfc_inst(struct s5p_mfc_dev *dev, struct s5p_mfc_ctx *ctx)
{
	int ret = 0;

	ret = s5p_mfc_hw_call(dev->mfc_ops, alloc_instance_buffer, ctx);
	if (ret) {
		mfc_err("Failed allocating instance buffer\n");
		goto err;
	}

	if (ctx->type == MFCINST_DECODER) {
		ret = s5p_mfc_hw_call(dev->mfc_ops,
					alloc_dec_temp_buffers, ctx);
		if (ret) {
			mfc_err("Failed allocating temporary buffers\n");
			goto err_free_inst_buf;
		}
	}

	s5p_mfc_try_ctx(ctx);
	if (s5p_mfc_wait_for_done_ctx(ctx)) {
		/* Error or timeout */
		mfc_err("Error getting instance from hardware\n");
		ret = -EIO;
		goto err_free_desc_buf;
	}

	mfc_debug(2, "Got instance number: %d\n", ctx->inst_no);
	return ret;

err_free_desc_buf:
	if (ctx->type == MFCINST_DECODER)
		s5p_mfc_hw_call(dev->mfc_ops, release_dec_desc_buffer, ctx);
err_free_inst_buf:
	s5p_mfc_hw_call(dev->mfc_ops, release_instance_buffer, ctx);
err:
	return ret;
}

void s5p_mfc_free_mfc_inst(struct s5p_mfc_dev *dev, struct s5p_mfc_ctx *ctx)
{
	s5p_mfc_hw_call(dev->mfc_ops, release_codec_buffers, ctx);
	s5p_mfc_hw_call(dev->mfc_ops, release_instance_buffer, ctx);
	if (ctx->type == MFCINST_DECODER)
		s5p_mfc_hw_call(dev->mfc_ops, release_dec_desc_buffer, ctx);
}

static void s5p_mfc_release_mfc_inst(struct s5p_mfc_dev *dev,
				     struct s5p_mfc_ctx *ctx)
{
	unsigned long flags;
	bool inst_released;

	/*
	 * At this point there might be still the timeout watchdog running
	 * and it might overwrite the state with MFCINST_ERROR. However, it
	 * will also set dev->hw_error, reset ctx->inst_no and fully reset
	 * the hardware, so that it no longer uses any resources acquired
	 * before the timeout. Both _try_ctx() and _wait_for_done_ctx() are
	 * designed to handle this case.
	 */
	spin_lock_irqsave(&dev->irqlock, flags);
	ctx->state = MFCINST_RETURN_INST;
	spin_unlock_irqrestore(&dev->irqlock, flags);

	s5p_mfc_try_ctx(ctx);
	if (!s5p_mfc_wait_for_done_ctx(ctx))
		return;

	/*
	 * Error handling here is really tricky because if this particular
	 * operation fails, the hardware might still keep referencing
	 * some resources we want to free. The sequence below is carefully
	 * crafted to make sure that the resources are released at any cost,
	 * including resetting the hardware.
	 */

	/*
	 * Even in case of error the instance might have been released
	 * for us, so bail out if this happened without affecting other
	 * instances.
	 */
	spin_lock_irqsave(&dev->irqlock, flags);
	inst_released = ctx->inst_no == MFC_NO_INSTANCE_SET;
	spin_unlock_irqrestore(&dev->irqlock, flags);

	if (inst_released)
		return;

	mfc_err("Err returning instance, resetting the hardware\n");

	/*
	 * The following will prevent scheduling new contexts
	 * and wait until the hardware completes processing current
	 * run or timeout watchdog kicks in.
	 */
	set_bit(0, &dev->hw_error);
	wait_event(dev->queue, !s5p_mfc_hw_is_locked(dev));

	/*
	 * If the watchdog wakes us up, we have no instance set anymore
	 * and we can bail out and let it do the work.
	 */
	spin_lock_irqsave(&dev->irqlock, flags);
	inst_released = ctx->inst_no == MFC_NO_INSTANCE_SET;
	spin_unlock_irqrestore(&dev->irqlock, flags);

	if (inst_released)
		return;

	/* Schedule the watchdog for instant execution otherwise. */
	WARN_ON(s5p_mfc_hw_trylock(dev));
	schedule_delayed_work(&dev->watchdog_work, 0);
}

void s5p_mfc_close_mfc_inst(struct s5p_mfc_dev *dev, struct s5p_mfc_ctx *ctx)
{
	/* Release hardware instance if needed. */
	s5p_mfc_release_mfc_inst(dev, ctx);

	/* Free resources */
	s5p_mfc_free_mfc_inst(dev, ctx);
}
