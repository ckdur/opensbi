/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *   Anup Patel <anup.patel@wdc.com>
 */

#include <libfdt.h>
#include <platform_override.h>
#include <sbi/riscv_asm.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_hartmask.h>
#include <sbi/sbi_platform.h>
#include <sbi/sbi_string.h>
#include <sbi_utils/fdt/fdt_domain.h>
#include <sbi_utils/fdt/fdt_fixup.h>
#include <sbi_utils/fdt/fdt_helper.h>
#include <sbi_utils/fdt/fdt_pmu.h>
#include <sbi_utils/irqchip/fdt_irqchip.h>
#include <sbi_utils/serial/fdt_serial.h>
#include <sbi_utils/timer/fdt_timer.h>
#include <sbi_utils/ipi/fdt_ipi.h>
#include <sbi_utils/reset/fdt_reset.h>

static const struct platform_override *generic_plat = NULL;
static const struct fdt_match *generic_plat_match = NULL;

extern struct sbi_platform platform;
static u32 generic_hart_index2id[SBI_HARTMASK_MAX_BITS] = { 0 };

/*
 * The fw_platform_init() function is called very early on the boot HART
 * OpenSBI reference firmwares so that platform specific code get chance
 * to update "platform" instance before it is used.
 *
 * The arguments passed to fw_platform_init() function are boot time state
 * of A0 to A4 register. The "arg0" will be boot HART id and "arg1" will
 * be address of FDT passed by previous booting stage.
 *
 * The return value of fw_platform_init() function is the FDT location. If
 * FDT is unchanged (or FDT is modified in-place) then fw_platform_init()
 * can always return the original FDT location (i.e. 'arg1') unmodified.
 */
unsigned long fw_platform_init(unsigned long arg0, unsigned long arg1,
				unsigned long arg2, unsigned long arg3,
				unsigned long arg4)
{
	const char *model;
	void *fdt = (void *)arg1;
	u32 hartid, hart_count = 0;
	int rc, root_offset, cpus_offset, cpu_offset, len;

	root_offset = fdt_path_offset(fdt, "/");
	if (root_offset < 0)
		goto fail;

	model = fdt_getprop(fdt, root_offset, "model", &len);
	if (model)
		sbi_strncpy(platform.name, model, sizeof(platform.name) - 1);

	if (generic_plat && generic_plat->features)
		platform.features = generic_plat->features(generic_plat_match);

	cpus_offset = fdt_path_offset(fdt, "/cpus");
	if (cpus_offset < 0)
		goto fail;

	fdt_for_each_subnode(cpu_offset, fdt, cpus_offset) {
		rc = fdt_parse_hart_id(fdt, cpu_offset, &hartid);
		if (rc)
			continue;

		if (SBI_HARTMASK_MAX_BITS <= hartid)
			continue;

		generic_hart_index2id[hart_count++] = hartid;
	}

	platform.hart_count = hart_count;

	/* Return original FDT pointer */
	return arg1;

fail:
	while (1)
		wfi();
}

static int generic_early_init(bool cold_boot)
{
	int rc;

	if (generic_plat && generic_plat->early_init) {
		rc = generic_plat->early_init(cold_boot, generic_plat_match);
		if (rc)
			return rc;
	}

	if (!cold_boot)
		return 0;

	return fdt_reset_init();
}

// fdt_cpu_timefreq_fixup - adds a simple timebase-frequency in /cpus
// Chipyard still does not support this one in master
void fdt_cpu_timefreq_fixup(void *fdt)
{
	int err, cpus_offset, cpu_offset, len;
	fdt32_t *pfreq;
	fdt32_t freq;

	err = fdt_open_into(fdt, fdt, fdt_totalsize(fdt) + 32);
	if (err < 0)
		return;

	cpus_offset = fdt_path_offset(fdt, "/cpus");
	if (cpus_offset < 0)
		return;
  
	fdt_for_each_subnode(cpu_offset, fdt, cpus_offset) {
	  pfreq = (fdt32_t *)fdt_getprop(fdt, cpu_offset, "timebase-frequency", &len);
	  if (len > 0 && pfreq) {
	    freq = *pfreq;
	    sbi_printf("\nSetting timebase-frequency to %d\n", fdt32_to_cpu(freq));
		  err = fdt_appendprop(fdt, cpus_offset, "timebase-frequency", &freq, 4);
		  break;
		}
	}
}

// fdt_debug_interrupts_fixup - removes the interrupts-extended from the debug
// Always triggers an exception
void fdt_debug_interrupts_fixup(void *fdt)
{
	int debug_offset;

	debug_offset = fdt_path_offset(fdt, "/soc/debug-controller");
	if (debug_offset < 0)
		return;

  fdt_delprop(fdt, debug_offset, "interrupts-extended");
}

// fdt_nodtbmem_fixup - removes the memory and replaces it with a rom
void fdt_nodtbmem_fixup(void *fdt)
{
  int na = fdt_address_cells(fdt, 0);
	int ns = fdt_size_cells(fdt, 0);
	fdt32_t reg[4];
	fdt32_t *val;
	int mem_offset, err, soc_offset, rom_offset;

	mem_offset = fdt_path_offset(fdt, "/memory@82200000");
	if (mem_offset < 0)
		return;

  err = fdt_del_node(fdt, mem_offset);
	if (err < 0)
		return;
  
  err = fdt_pack(fdt);
	if (err < 0)
		return;

	soc_offset = fdt_path_offset(fdt, "/soc");
	if (soc_offset < 0)
		return;
  
  rom_offset = fdt_add_subnode(fdt, soc_offset, "rom@82200000");
  if (rom_offset < 0)
	  return;
	fdt_appendprop(fdt, rom_offset, "compatible", "sifive,maskrom0", sizeof("sifive,maskrom0"));
	
	/* encode the <reg> property value */
	val = reg;
	if (na > 1)
		*val++ = cpu_to_fdt32(0x0);
	*val++ = cpu_to_fdt32(0x82200000);
	if (ns > 1)
		*val++ = cpu_to_fdt32(0x0);
	*val++ = cpu_to_fdt32(0x4000);
	
	fdt_appendprop(fdt, rom_offset, "reg", reg,
			  (na + ns) * sizeof(fdt32_t));
}

static int generic_final_init(bool cold_boot)
{
	void *fdt;
	int rc;

	if (generic_plat && generic_plat->final_init) {
		rc = generic_plat->final_init(cold_boot, generic_plat_match);
		if (rc)
			return rc;
	}

	if (!cold_boot)
		return 0;

	fdt = sbi_scratch_thishart_arg1_ptr();

	fdt_cpu_fixup(fdt);
	fdt_fixups(fdt);
	fdt_domain_fixup(fdt);
	fdt_cpu_timefreq_fixup(fdt);
	fdt_debug_interrupts_fixup(fdt);

	if (generic_plat && generic_plat->fdt_fixup) {
		rc = generic_plat->fdt_fixup(fdt, generic_plat_match);
		if (rc)
			return rc;
	}

	return 0;
}

static void generic_early_exit(void)
{
	if (generic_plat && generic_plat->early_exit)
		generic_plat->early_exit(generic_plat_match);
}

static void generic_final_exit(void)
{
	if (generic_plat && generic_plat->final_exit)
		generic_plat->final_exit(generic_plat_match);
}

static int generic_domains_init(void)
{
	return fdt_domains_populate(sbi_scratch_thishart_arg1_ptr());
}

static u64 generic_tlbr_flush_limit(void)
{
	if (generic_plat && generic_plat->tlbr_flush_limit)
		return generic_plat->tlbr_flush_limit(generic_plat_match);
	return SBI_PLATFORM_TLB_RANGE_FLUSH_LIMIT_DEFAULT;
}

static int generic_pmu_init(void)
{
	return fdt_pmu_setup(sbi_scratch_thishart_arg1_ptr());
}

static uint64_t generic_pmu_xlate_to_mhpmevent(uint32_t event_idx,
					       uint64_t data)
{
	uint64_t evt_val = 0;

	/* data is valid only for raw events and is equal to event selector */
	if (event_idx == SBI_PMU_EVENT_RAW_IDX)
		evt_val = data;
	else {
		/**
		 * Generic platform follows the SBI specification recommendation
		 * i.e. zero extended event_idx is used as mhpmevent value for
		 * hardware general/cache events if platform does't define one.
		 */
		evt_val = fdt_pmu_get_select_value(event_idx);
		if (!evt_val)
			evt_val = (uint64_t)event_idx;
	}

	return evt_val;
}

const struct sbi_platform_operations platform_ops = {
	.early_init		= generic_early_init,
	.final_init		= generic_final_init,
	.early_exit		= generic_early_exit,
	.final_exit		= generic_final_exit,
	.domains_init		= generic_domains_init,
	.console_init		= fdt_serial_init,
	.irqchip_init		= fdt_irqchip_init,
	.irqchip_exit		= fdt_irqchip_exit,
	.ipi_init		= fdt_ipi_init,
	.ipi_exit		= fdt_ipi_exit,
	.pmu_init		= generic_pmu_init,
	.pmu_xlate_to_mhpmevent = generic_pmu_xlate_to_mhpmevent,
	.get_tlbr_flush_limit	= generic_tlbr_flush_limit,
	.timer_init		= fdt_timer_init,
	.timer_exit		= fdt_timer_exit,
};

struct sbi_platform platform = {
	.opensbi_version	= OPENSBI_VERSION,
	.platform_version	= SBI_PLATFORM_VERSION(0x0, 0x01),
	.name			= "RATONA",
	.features		= SBI_PLATFORM_DEFAULT_FEATURES,
	.hart_count		= SBI_HARTMASK_MAX_BITS,
	.hart_index2id		= generic_hart_index2id,
	.hart_stack_size	= SBI_PLATFORM_DEFAULT_HART_STACK_SIZE,
	.platform_ops_addr	= (unsigned long)&platform_ops
};
