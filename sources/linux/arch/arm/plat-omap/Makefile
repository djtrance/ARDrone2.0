#
# Makefile for the linux kernel.
#

# Common support
obj-y := common.o sram.o clock.o devices.o dma.o mux.o \
	 usb.o fb.o io.o hdmi_lib.o
obj-m :=
obj-n :=
obj-  :=

# OCPI interconnect support for 1710, 1610 and 5912
obj-$(CONFIG_ARCH_OMAP16XX) += ocpi.o

# omap_device support (OMAP2+ only at the moment)
obj-$(CONFIG_ARCH_OMAP2) += omap_device.o
obj-$(CONFIG_ARCH_OMAP3) += omap_device.o
obj-$(CONFIG_ARCH_OMAP4) += omap_device.o

obj-$(CONFIG_OMAP_MCBSP) += mcbsp.o
iommuex-y		 += iodmm.o iovmm.o iommu.o
ifdef CONFIG_OMAP_IOMMU
obj-y           	 += $(iommuex-y)
endif
obj-$(CONFIG_OMAP_IOMMU_DEBUG) += iommu-debug.o

obj-$(CONFIG_CPU_FREQ) += cpu-omap.o
obj-$(CONFIG_OMAP_DM_TIMER) += dmtimer.o
obj-$(CONFIG_OMAP_DEBUG_DEVICES) += debug-devices.o
obj-$(CONFIG_OMAP_DEBUG_LEDS) += debug-leds.o
obj-$(CONFIG_HDQ_MASTER_OMAP) += hdq.o
i2c-omap-$(CONFIG_I2C_OMAP) := i2c.o
obj-y += $(i2c-omap-m) $(i2c-omap-y)

# OMAP mailbox framework
obj-$(CONFIG_OMAP_MBOX_FWK) += mailbox.o
obj-$(CONFIG_OMAP_REMOTE_PROC) += remoteproc.o

obj-$(CONFIG_OMAP_PM_NOOP) += omap-pm-noop.o
obj-$(CONFIG_ARCH_OMAP4) += hwspinlock.o
obj-$(CONFIG_OMAP_PM_SRF) += omap-pm-srf.o \
				resource.o

