ifeq ($(CONFIG_SMP),y)
obj-$(CONFIG_ARCH_MEDIATEK) += platsmp.o
endif
obj-$(CONFIG_ARCH_MEDIATEK) += mediatek.o
obj-$(CONFIG_MACH_MT8163) += mt8163.o
CFLAGS_mt8163.o := -I$(srctree)/drivers/misc/mediatek/base/power/mt8163
