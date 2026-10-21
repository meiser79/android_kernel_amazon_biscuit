/*
 * MT8163 running a 32-bit kernel
 *
 * The secondary CPUs are started through PSCI, like on arm64, but their
 * MTCMOS power domains are switched by the kernel: the arm64 "mt-boot"
 * cpu_operations in drivers/misc/mediatek/base/power/mt8163/mt_psci.c do
 * the same. The arm setup code prefers psci_smp_ops over any DT
 * enable-method, so the ops are installed from .smp_init.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <asm/mach/arch.h>
#include <asm/psci.h>
#include <asm/smp.h>

#include "mtcmos.h"

#ifdef CONFIG_SMP
static int (*psci_boot_secondary)(unsigned int cpu, struct task_struct *idle);
#ifdef CONFIG_HOTPLUG_CPU
static int (*psci_kill)(unsigned int cpu);
#endif

static void __init mt8163_smp_prepare_cpus(unsigned int max_cpus)
{
	spm_mtcmos_cpu_init();
}

static int mt8163_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	int ret;

	ret = psci_boot_secondary(cpu, idle);
	if (ret < 0)
		return ret;

	return spm_mtcmos_ctrl_cpu(cpu, STA_POWER_ON, 1);
}

#ifdef CONFIG_HOTPLUG_CPU
static int mt8163_cpu_kill(unsigned int cpu)
{
	if (!psci_kill(cpu))
		pr_warn("CPU%d may not have shut down cleanly\n", cpu);

	/* platform_cpu_kill() treats 0 as failure */
	return !spm_mtcmos_ctrl_cpu(cpu, STA_POWER_DOWN, 1);
}
#endif

static struct smp_operations mt8163_smp_ops __initdata;

static bool __init mt8163_smp_init(void)
{
	if (!psci_smp_available())
		return false;

	mt8163_smp_ops = psci_smp_ops;
	psci_boot_secondary = psci_smp_ops.smp_boot_secondary;
	mt8163_smp_ops.smp_prepare_cpus = mt8163_smp_prepare_cpus;
	mt8163_smp_ops.smp_boot_secondary = mt8163_boot_secondary;
#ifdef CONFIG_HOTPLUG_CPU
	psci_kill = psci_smp_ops.cpu_kill;
	mt8163_smp_ops.cpu_kill = mt8163_cpu_kill;
#endif
	smp_set_ops(&mt8163_smp_ops);

	return true;
}
#endif

static const char * const mt8163_dt_compat[] __initconst = {
	"mediatek,mt8163",
	NULL,
};

DT_MACHINE_START(MT8163_DT, "MediaTek MT8163 (Device Tree)")
	.dt_compat	= mt8163_dt_compat,
#ifdef CONFIG_SMP
	.smp_init	= smp_init_ops(mt8163_smp_init),
#endif
MACHINE_END
