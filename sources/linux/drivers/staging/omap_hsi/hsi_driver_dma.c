/*
 * hsi_driver_dma.c
 *
 * Implements HSI low level interface driver functionality with DMA support.
 *
 * Copyright (C) 2007-2008 Nokia Corporation. All rights reserved.
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * Author: Carlos Chinea <carlos.chinea@nokia.com>
 * Author: Sebastien JAN <s-jan@ti.com>
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/dma-mapping.h>
#include "hsi_driver.h"

#define HSI_SYNC_WRITE	0
#define HSI_SYNC_READ	1
#define HSI_L3_TPUT	13428 /* 13428 KiB/s => ~110 Mbit/s*/

static unsigned char hsi_sync_table[2][2][8] = {
	{
		{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08},
		{0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x00}
	}, {
		{0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17},
		{0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f}
	}
};

/**
 * hsi_get_free_lch - Get a free GDD(DMA)logical channel
 * @hsi_ctrl- HSI controller of the GDD.
 *
 * Needs to be called holding the hsi_controller lock
 *
 * Return a free logical channel number. If there is no free lch
 * then returns an out of range value
 */
static unsigned int hsi_get_free_lch(struct hsi_dev *hsi_ctrl)
{
	unsigned int enable_reg;
	unsigned int i;
	unsigned int lch = hsi_ctrl->last_gdd_lch;

	enable_reg = hsi_inl(hsi_ctrl->base, HSI_SYS_GDD_MPU_IRQ_ENABLE_REG);
	for (i = 1; i <= hsi_ctrl->gdd_chan_count; i++) {
		lch = (lch + i) & (hsi_ctrl->gdd_chan_count - 1);
		if (!(enable_reg & HSI_GDD_LCH(lch))) {
			hsi_ctrl->last_gdd_lch = lch;
			return lch;
		}
	}

	return lch;
}

/**
 * hsi_driver_write_dma - Program GDD [DMA] to write data from memory to
 * the hsi channel buffer.
 * @hsi_channel - pointer to the hsi_channel to write data to.
 * @data - 32-bit word pointer to the data.
 * @size - Number of 32bit words to be transfered.
 *
 * hsi_controller lock must be held before calling this function.
 *
 * Return 0 on success and < 0 on error.
 */
int hsi_driver_write_dma(struct hsi_channel *hsi_channel, u32 *data,
			 unsigned int size)
{
	struct hsi_dev *hsi_ctrl = hsi_channel->hsi_port->hsi_controller;
	void __iomem *base = hsi_ctrl->base;
	unsigned int port = hsi_channel->hsi_port->port_number;
	unsigned int channel = hsi_channel->channel_number;
	unsigned int sync;
	int lch;
	dma_addr_t dma_data;
	u16 tmp;

	if ((size < 1) || (data == NULL))
		return -EINVAL;

	lch = hsi_get_free_lch(hsi_ctrl);
	if (lch >= hsi_ctrl->gdd_chan_count) {
		dev_err(hsi_ctrl->dev, "No free GDD logical channels.\n");
		return -EBUSY;	/* No free GDD logical channels. */
	}

	/* NOTE: Getting a free gdd logical channel and
	 * reserve it must be done atomicaly. */
	hsi_channel->write_data.lch = lch;

	/* Sync is required for SSI but not for HSI */
	sync = hsi_sync_table[HSI_SYNC_WRITE][port - 1][channel];

	dma_data = dma_map_single(hsi_ctrl->dev, data, size * 4,
					DMA_TO_DEVICE);

	tmp = HSI_SRC_SINGLE_ACCESS0 |
		HSI_SRC_MEMORY_PORT |
		HSI_DST_SINGLE_ACCESS0 |
		HSI_DST_PERIPHERAL_PORT |
		HSI_DATA_TYPE_S32;
	hsi_outw(tmp, base, HSI_GDD_CSDP_REG(lch));

	tmp = HSI_SRC_AMODE_POSTINC | HSI_DST_AMODE_CONST | sync;
	hsi_outw(tmp, base, HSI_GDD_CCR_REG(lch));

	hsi_outw((HSI_BLOCK_IE | HSI_TOUT_IE), base, HSI_GDD_CICR_REG(lch));

	hsi_outl(channel, base, HSI_GDD_CDSA_REG(lch));

	hsi_outl(dma_data, base, HSI_GDD_CSSA_REG(lch));
	hsi_outw(size, base, HSI_GDD_CEN_REG(lch));

	hsi_outl_or(HSI_GDD_LCH(lch), base, HSI_SYS_GDD_MPU_IRQ_ENABLE_REG);
	hsi_outw_or(HSI_CCR_ENABLE, base, HSI_GDD_CCR_REG(lch));

	return 0;
}

/**
 * hsi_driver_read_dma - Program GDD [DMA] to write data to memory from
 * the hsi channel buffer.
 * @hsi_channel - pointer to the hsi_channel to read data from.
 * @data - 32-bit word pointer where to store the incoming data.
 * @size - Number of 32bit words to be transfered to the buffer.
 *
 * hsi_controller lock must be held before calling this function.
 *
 * Return 0 on success and < 0 on error.
 */
int hsi_driver_read_dma(struct hsi_channel *hsi_channel, u32 *data,
			unsigned int count)
{
	struct hsi_dev *hsi_ctrl = hsi_channel->hsi_port->hsi_controller;
	void __iomem *base = hsi_ctrl->base;
	unsigned int port = hsi_channel->hsi_port->port_number;
	unsigned int channel = hsi_channel->channel_number;
	unsigned int sync;
	unsigned int lch;
	dma_addr_t dma_data;
	u16 tmp;

	lch = hsi_get_free_lch(hsi_ctrl);
	if (lch >= hsi_ctrl->gdd_chan_count) {
		dev_err(hsi_ctrl->dev, "No free GDD logical channels.\n");
		return -EBUSY;	/* No free GDD logical channels. */
	}

	/* When DMA is used for Rx, disable the Rx Interrupt.
	 * (else DATAAVAILLABLE event would get triggered on first
	 * received data word)
	 * (By default, Rx interrupt is active for polling feature)
	 */
	hsi_driver_disable_read_interrupt(hsi_channel);

	/*
	 * NOTE: Gettting a free gdd logical channel and
	 * reserve it must be done atomicaly.
	 */
	hsi_channel->read_data.lch = lch;

	/* Sync is required for SSI but not for HSI */
	sync = hsi_sync_table[HSI_SYNC_READ][port - 1][channel];

	dma_data = dma_map_single(hsi_ctrl->dev, data, count * 4,
					DMA_FROM_DEVICE);

	tmp = HSI_DST_SINGLE_ACCESS0 |
		HSI_DST_MEMORY_PORT |
		HSI_SRC_SINGLE_ACCESS0 |
		HSI_SRC_PERIPHERAL_PORT |
		HSI_DATA_TYPE_S32;
	hsi_outw(tmp, base, HSI_GDD_CSDP_REG(lch));

	tmp = HSI_DST_AMODE_POSTINC | HSI_SRC_AMODE_CONST | sync;
	hsi_outw(tmp, base, HSI_GDD_CCR_REG(lch));

	hsi_outw((HSI_BLOCK_IE | HSI_TOUT_IE), base, HSI_GDD_CICR_REG(lch));

	hsi_outl(channel, base, HSI_GDD_CSSA_REG(lch));

	hsi_outl(dma_data, base, HSI_GDD_CDSA_REG(lch));
	hsi_outw(count, base, HSI_GDD_CEN_REG(lch));

	hsi_outl_or(HSI_GDD_LCH(lch), base, HSI_SYS_GDD_MPU_IRQ_ENABLE_REG);
	hsi_outw_or(HSI_CCR_ENABLE, base, HSI_GDD_CCR_REG(lch));

	return 0;
}

void hsi_driver_cancel_write_dma(struct hsi_channel *hsi_ch)
{
	int lch = hsi_ch->write_data.lch;
	unsigned int port = hsi_ch->hsi_port->port_number;
	unsigned int channel = hsi_ch->channel_number;
	struct hsi_dev *hsi_ctrl = hsi_ch->hsi_port->hsi_controller;
	u32 ccr;
	long buff_offset;

	if (lch < 0)
		return;

	ccr = hsi_inw(hsi_ctrl->base, HSI_GDD_CCR_REG(lch));
	if (!(ccr & HSI_CCR_ENABLE)) {
		dev_dbg(&hsi_ch->dev->device, LOG_NAME "Write cancel on not "
		"enabled logical channel %d CCR REG 0x%08X\n", lch, ccr);
		return;
	}

	hsi_outw_and(~HSI_CCR_ENABLE, hsi_ctrl->base, HSI_GDD_CCR_REG(lch));
	hsi_outl_and(~HSI_GDD_LCH(lch), hsi_ctrl->base,
						HSI_SYS_GDD_MPU_IRQ_ENABLE_REG);
	hsi_outl(HSI_GDD_LCH(lch), hsi_ctrl->base,
						HSI_SYS_GDD_MPU_IRQ_STATUS_REG);

	buff_offset = hsi_hst_bufstate_f_reg(hsi_ctrl, port, channel);
	if (buff_offset >= 0)
		hsi_outl_and(~HSI_BUFSTATE_CHANNEL(channel), hsi_ctrl->base,
								buff_offset);

	hsi_reset_ch_write(hsi_ch);
}

void hsi_driver_cancel_read_dma(struct hsi_channel *hsi_ch)
{
	int lch = hsi_ch->read_data.lch;
	struct hsi_dev *hsi_ctrl = hsi_ch->hsi_port->hsi_controller;
	unsigned int port = hsi_ch->hsi_port->port_number;
	unsigned int channel = hsi_ch->channel_number;
	u32 reg;
	long buff_offset;

	if (lch < 0)
		return;

	/* DMA transfer is over, re-enable default mode
	 * (Interrupts for polling feature)
	 */
	hsi_driver_read_interrupt(hsi_ch, NULL);

	reg = hsi_inw(hsi_ctrl->base, HSI_GDD_CCR_REG(lch));
	if (!(reg & HSI_CCR_ENABLE)) {
		dev_dbg(&hsi_ch->dev->device, LOG_NAME "Read cancel on not "
		"enable logical channel %d CCR REG 0x%08X\n", lch, reg);
		return;
	}

	hsi_outw_and(~HSI_CCR_ENABLE, hsi_ctrl->base, HSI_GDD_CCR_REG(lch));
	hsi_outl_and(~HSI_GDD_LCH(lch), hsi_ctrl->base,
						HSI_SYS_GDD_MPU_IRQ_ENABLE_REG);
	hsi_outl(HSI_GDD_LCH(lch), hsi_ctrl->base,
						HSI_SYS_GDD_MPU_IRQ_STATUS_REG);

	buff_offset = hsi_hsr_bufstate_f_reg(hsi_ctrl, port, channel);
	if (buff_offset >= 0)
		hsi_outl_and(~HSI_BUFSTATE_CHANNEL(channel), hsi_ctrl->base,
								buff_offset);

	hsi_reset_ch_read(hsi_ch);
}

/**
 * hsi_get_info_from_gdd_lch - Retrieve channels information from DMA channel
 * @hsi_ctrl - HSI device control structure
 * @lch - DMA logical channel
 * @port - HSI port
 * @channel - HSI channel
 * @is_read_path - channel is used for reading
 *
 * Updates the port, channel and is_read_path parameters depending on the
 * lch DMA channel status.
 *
 * Return 0 on success and < 0 on error.
 */
int hsi_get_info_from_gdd_lch(struct hsi_dev *hsi_ctrl, unsigned int lch,
	unsigned int *port, unsigned int *channel, unsigned int *is_read_path)
{
	int i_ports;
	int i_chans;
	int err = -1;

	for (i_ports = 0; i_ports < HSI_MAX_PORTS; i_ports++)
		for (i_chans = 0; i_chans < HSI_PORT_MAX_CH; i_chans++)
			if (hsi_ctrl->hsi_port[i_ports].
				hsi_channel[i_chans].read_data.lch == lch) {
				*is_read_path = 1;
				*port = i_ports + 1;
				*channel = i_chans;
				err = 0;
				goto get_info_bk;
			} else if (hsi_ctrl->hsi_port[i_ports].
				hsi_channel[i_chans].write_data.lch == lch) {
				*is_read_path = 0;
				*port = i_ports + 1;
				*channel = i_chans;
				err = 0;
				goto get_info_bk;
			}
get_info_bk:
	return err;
}

static void do_hsi_gdd_lch(struct hsi_dev *hsi_ctrl, unsigned int gdd_lch)
{
	void __iomem *base = hsi_ctrl->base;
	struct hsi_channel *ch;
	unsigned int port;
	unsigned int channel;
	unsigned int is_read_path;
	u32 gdd_csr;
	dma_addr_t dma_h;
	size_t size;

	if (hsi_get_info_from_gdd_lch(hsi_ctrl, gdd_lch, &port, &channel,
							&is_read_path) < 0) {
		dev_err(hsi_ctrl->dev, "Unable to match the DMA channel %d with"
						" an HSI channel\n", gdd_lch);
		return;
	}
/* FIXME: to remove when validated: */
	else {
		dev_dbg(hsi_ctrl->dev, "DMA event on gdd_lch=%d => port=%d, "
			"channel=%d, read=%d\n", gdd_lch, port, channel,
								is_read_path);
	}

	spin_lock(&hsi_ctrl->lock);

	hsi_outl_and(~HSI_GDD_LCH(gdd_lch), base,
						HSI_SYS_GDD_MPU_IRQ_ENABLE_REG);
	gdd_csr = hsi_inw(base, HSI_GDD_CSR_REG(gdd_lch));

	if (!(gdd_csr & HSI_CSR_TOUT)) {
		if (is_read_path) { /* Read path */
			dma_h = hsi_inl(base, HSI_GDD_CDSA_REG(gdd_lch));
			size = hsi_inw(base, HSI_GDD_CEN_REG(gdd_lch)) * 4;
			dma_sync_single_for_cpu(hsi_ctrl->dev, dma_h, size,
							DMA_FROM_DEVICE);
			dma_unmap_single(hsi_ctrl->dev, dma_h, size,
						DMA_FROM_DEVICE);
			ch = ctrl_get_ch(hsi_ctrl, port, channel);
			hsi_reset_ch_read(ch);
			/* DMA transfer is over, re-enable default mode
			 * (interrupts for polling feature)
			 */
			hsi_driver_read_interrupt(ch, NULL);
			spin_unlock(&hsi_ctrl->lock);
			ch->read_done(ch->dev, size / 4);
		} else {
			dma_h = hsi_inl(base, HSI_GDD_CSSA_REG(gdd_lch));
			size = hsi_inw(base, HSI_GDD_CEN_REG(gdd_lch)) * 4;
			dma_unmap_single(hsi_ctrl->dev, dma_h, size,
						DMA_TO_DEVICE);
			ch = ctrl_get_ch(hsi_ctrl, port, channel);
			hsi_reset_ch_write(ch);
			spin_unlock(&hsi_ctrl->lock);
			ch->write_done(ch->dev, size / 4);
		}
	} else {
		dev_err(hsi_ctrl->dev, "Error  on GDD transfer "
				"on gdd channel %d\n", gdd_lch);
		spin_unlock(&hsi_ctrl->lock);
		hsi_port_event_handler(&hsi_ctrl->hsi_port[port - 1],
							HSI_EVENT_ERROR, NULL);
	}
}

static void do_hsi_gdd_tasklet(unsigned long device)
{
	struct hsi_dev *hsi_ctrl = (struct hsi_dev *)device;
	void __iomem *base = hsi_ctrl->base;
	unsigned int gdd_lch = 0;
	u32 status_reg = 0;
	u32 lch_served = 0;
	unsigned int gdd_max_count = hsi_ctrl->gdd_chan_count;

	status_reg = hsi_inl(base, HSI_SYS_GDD_MPU_IRQ_STATUS_REG);

	for (gdd_lch = 0; gdd_lch < gdd_max_count; gdd_lch++) {
		if (status_reg & HSI_GDD_LCH(gdd_lch)) {
			do_hsi_gdd_lch(hsi_ctrl, gdd_lch);
			lch_served |= HSI_GDD_LCH(gdd_lch);
		}
	}

	hsi_outl(lch_served, base, HSI_SYS_GDD_MPU_IRQ_STATUS_REG);

	status_reg = hsi_inl(base, HSI_SYS_GDD_MPU_IRQ_STATUS_REG);
	status_reg &= hsi_inl(base, HSI_SYS_GDD_MPU_IRQ_ENABLE_REG);

	if (status_reg)
		tasklet_hi_schedule(&hsi_ctrl->hsi_gdd_tasklet);
	else
		enable_irq(hsi_ctrl->gdd_irq);
}

static irqreturn_t hsi_gdd_mpu_handler(int irq, void *hsi_controller)
{
	struct hsi_dev *hsi_ctrl = hsi_controller;

	tasklet_hi_schedule(&hsi_ctrl->hsi_gdd_tasklet);
	disable_irq_nosync(hsi_ctrl->gdd_irq);

	return IRQ_HANDLED;
}

int __init hsi_gdd_init(struct hsi_dev *hsi_ctrl, const char *irq_name)
{
	tasklet_init(&hsi_ctrl->hsi_gdd_tasklet, do_hsi_gdd_tasklet,
						(unsigned long)hsi_ctrl);
	if (request_irq(hsi_ctrl->gdd_irq, hsi_gdd_mpu_handler, IRQF_DISABLED,
						irq_name, hsi_ctrl) < 0) {
		dev_err(hsi_ctrl->dev, "FAILED to request GDD IRQ %d\n",
							hsi_ctrl->gdd_irq);
		return -EBUSY;
	}

	return 0;
}

void hsi_gdd_exit(struct hsi_dev *hsi_ctrl)
{
	tasklet_disable(&hsi_ctrl->hsi_gdd_tasklet);
	free_irq(hsi_ctrl->gdd_irq, hsi_ctrl);
}
