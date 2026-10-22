/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The 5.4 interfaces the legacy FastRPC driver uses, on 6.12: SSR through
 * the qcom remoteproc notifiers, PDR through the in-kernel PDR helpers,
 * memory assignment through SCM, and no ramdumps.
 */
#ifndef ADSPRPC_612_H
#define ADSPRPC_612_H

#include <linux/bits.h>
#include <linux/dma-map-ops.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/soc/qcom/pdr.h>

/* QGKI only */
#define DMA_ATTR_SKIP_ZEROING		0
#define DMA_ATTR_EXEC_MAPPING		0
#define DMA_ATTR_IOMMU_USE_UPSTREAM_HINT 0
#define ION_FLAG_SECURE			(1U << 31)

#define SUBSYS_BEFORE_SHUTDOWN		QCOM_SSR_BEFORE_SHUTDOWN
#define SUBSYS_AFTER_SHUTDOWN		QCOM_SSR_AFTER_SHUTDOWN
#define SUBSYS_BEFORE_POWERUP		QCOM_SSR_BEFORE_POWERUP
#define SUBSYS_AFTER_POWERUP		QCOM_SSR_AFTER_POWERUP
/* never sent on 6.12 */
#define SUBSYS_RAMDUMP_NOTIFICATION	0x100

#define SERVREG_NOTIF_SERVICE_STATE_DOWN_V01	SERVREG_SERVICE_STATE_DOWN
#define SERVREG_NOTIF_SERVICE_STATE_UP_V01	SERVREG_SERVICE_STATE_UP
#define SERVREG_NOTIF_SERVICE_STATE_UNINIT_V01	SERVREG_SERVICE_STATE_UNINIT

static inline const char *ssr_name(const char *subsys)
{
	if (!strcmp(subsys, "adsp"))
		return "lpass";
	if (!strcmp(subsys, "modem"))
		return "mpss";
	if (!strcmp(subsys, "slpi"))
		return "dsps";
	return subsys;
}

static inline void *subsys_notif_register_notifier(const char *subsys,
						   struct notifier_block *nb)
{
	return qcom_register_ssr_notifier(ssr_name(subsys), nb);
}

static inline int subsys_notif_unregister_notifier(void *handle,
						   struct notifier_block *nb)
{
	return qcom_unregister_ssr_notifier(handle, nb);
}

static inline int subsystem_restart(const char *name)
{
	pr_warn("adsprpc: can\x27t restart %s on this kernel\n", name);
	return -EOPNOTSUPP;
}

static inline int hyp_assign_phys(phys_addr_t addr, u64 size, int *src_vms,
				  int src_n, int *dst_vms, int *dst_perms, int dst_n)
{
	struct qcom_scm_vmperm newvm[4];
	u64 srcvm = 0;
	int i;

	if (dst_n > ARRAY_SIZE(newvm))
		return -EINVAL;
	for (i = 0; i < src_n; i++)
		srcvm |= BIT_ULL(src_vms[i]);
	for (i = 0; i < dst_n; i++) {
		newvm[i].vmid = dst_vms[i];
		newvm[i].perm = dst_perms[i];
	}
	return qcom_scm_assign_mem(addr, size, &srcvm, newvm, dst_n);
}

struct ramdump_segment {
	char *name;
	unsigned long address;
	void __iomem *v_address;
	unsigned long size;
};

static inline void *create_ramdump_device(const char *name, struct device *dev)
{
	return NULL;
}

static inline int do_minidump(void *handle, struct ramdump_segment *segs, int n)
{
	return -ENODEV;
}

#endif
