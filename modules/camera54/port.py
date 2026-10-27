#!/usr/bin/env python3
"""Port redwood's 5.4 camera driver (LineageOS sm8350 techpack/camera) to
6.12. Run on a fresh copy of the drivers/ directory; every change is made
here so the port can be redone from the LineageOS source."""
import os, re, sys

root = sys.argv[1]

def files(exts=(".c", ".h")):
    for d, _, fs in os.walk(root):
        for f in fs:
            if f.endswith(exts):
                yield os.path.join(d, f)

def drop_returns(body, value_ok=False):
    # a void function: "return x;" becomes "return;"
    return re.sub(r"\breturn\s+[^;]+;", "return;", body)

def func_span(s, start):
    i = s.index("{", start)
    depth = 0
    for k in range(i, len(s)):
        if s[k] == "{":
            depth += 1
        elif s[k] == "}":
            depth -= 1
            if depth == 0:
                return i, k + 1
    raise ValueError

remove_re = re.compile(r"\.remove\s*=\s*(\w+)")
probe_re = re.compile(r"\.probe\s*=\s*(\w+)")
removes, probes = set(), set()
for p in files((".c",)):
    t = open(p, errors="replace").read()
    removes |= set(remove_re.findall(t))
    probes |= set(probe_re.findall(t))

def fix_calls(s, fn, nargs, extra):
    # add an argument to calls of fn that have nargs arguments
    out, i = [], 0
    for m in re.finditer(r"\b" + fn + r"\s*\(", s):
        if m.start() < i:
            continue
        j, depth, commas = m.end(), 1, 0
        while depth:
            c = s[j]
            if c in "([{":
                depth += 1
            elif c in ")]}":
                depth -= 1
            elif c == "," and depth == 1:
                commas += 1
            j += 1
        if commas + 1 == nargs:
            out.append(s[i:j - 1] + ", " + extra + ")")
        else:
            out.append(s[i:j])
        i = j
    out.append(s[i:])
    return "".join(out)

changed = 0
for p in files():
    s = o = open(p, errors="replace").read()

    s = s.replace("#include <linux/dma-contiguous.h>", "#include <linux/dma-map-ops.h>")
    s = s.replace("#include <linux/dma-iommu.h>", "")
    s = re.sub(r"([\w\->.\[\]]+)\s*=\s*(debugfs_create_(?:bool|u8|u16|u32|u64|x8|x16|x32|x64|size_t|atomic_t|ulong)\s*\()", r"\2", s)

    # struct timespec / ktime_to_timespec are 64-bit only now
    s = re.sub(r"\bstruct timespec\b(?!64)", "struct timespec64", s)
    s = re.sub(r"\bktime_to_timespec\(", "ktime_to_timespec64(", s)

    # dma-buf: mapping without the reservation lock held
    s = re.sub(r"\bdma_buf_map_attachment\(", "dma_buf_map_attachment_unlocked(", s)
    s = re.sub(r"\bdma_buf_unmap_attachment\(", "dma_buf_unmap_attachment_unlocked(", s)
    s = re.sub(r"\bdma_buf_vmap\(", "cam_dma_buf_vmap(", s)
    s = re.sub(r"\bdma_buf_vunmap\(", "cam_dma_buf_vunmap(", s)

    # iommu_map() and iommu_map_sg() take a gfp
    s = fix_calls(s, "iommu_map", 5, "GFP_KERNEL")
    s = fix_calls(s, "iommu_map_sg", 5, "GFP_KERNEL")

    # list_sort() compares const list heads
    s = re.sub(r"(int\s+\w+\s*\(\s*void\s*\*\s*\w+\s*,\s*)struct list_head\s*\*\s*(\w+)\s*,\s*struct list_head\s*\*\s*(\w+)\s*\)",
               r"\1const struct list_head *\2, const struct list_head *\3)", s)

    for name in removes:
        for m in list(re.finditer(r"(?:static\s+)?(int|int32_t)\s+" + name + r"\s*\(\s*struct\s+(?:platform_device|i2c_client|spi_device)\s*\*\s*\w*\s*\)", s))[::-1]:
            tail = s[m.end():m.end() + 40].lstrip()
            head = s[m.start():m.end()]
            head = re.sub(r"\b(int|int32_t)\s+" + name, "void " + name, head, 1)
            if tail.startswith("{"):
                a, b = func_span(s, m.end())
                s = s[:m.start()] + head + s[m.end():a] + drop_returns(s[a:b]) + s[b:]
            else:
                s = s[:m.start()] + head + s[m.end():]

    for name in probes:
        m = re.search(r"((?:static\s+)?(?:int|int32_t)\s+" + name + r"\s*\(\s*struct\s+i2c_client\s*\*\s*(\w+)\s*),\s*const\s+struct\s+i2c_device_id\s*\*\s*(\w+)\s*\)", s)
        if not m:
            continue
        client, idv = m.group(2), m.group(3)
        a, b = func_span(s, m.end())
        body = s[a:b]
        if re.search(r"\b" + idv + r"\b", body):
            body = "{\n\tconst struct i2c_device_id *" + idv + " = i2c_client_get_device_id(" + client + ");\n" + body[1:]
        s = s[:m.start()] + m.group(1) + ")" + s[m.end():a] + body + s[b:]

    if s != o:
        open(p, "w").write(s)
        changed += 1

# trace: the 5.4 tree path doesn't exist here
t = os.path.join(root, "cam_utils", "cam_trace.h")
s = open(t).read()
s = re.sub(r"#define TRACE_INCLUDE_PATH .*", "#define TRACE_INCLUDE_PATH .", s)
s = re.sub(r"#define TRACE_INCLUDE_FILE .*", "#define TRACE_INCLUDE_FILE cam_trace", s)
# __assign_str() takes only the field since 6.10
s = re.sub(r"__assign_str\((\w+),[^;]*\);", r"__assign_str(\1);", s)
open(t, "w").write(s)
print("ported", changed, "files")

# interconnects: remember the node each bus client's src/dst came from
def edit(rel, pairs):
    f = os.path.join(root, rel)
    t = open(f).read()
    for a, b in pairs:
        if a not in t:
            raise SystemExit("port.py: %s: %r not found" % (rel, a[:60]))
        t = t.replace(a, b)
    open(f, "w").write(t)

edit("cam_utils/cam_soc_bus.h", [("\tuint32_t dst_id;\n\tint num_usecases;",
     "\tuint32_t dst_id;\n\tstruct device_node *icc_np;\n\tint num_usecases;")])
edit("cam_cpas/cam_cpas_soc.c", [
    ("\t\t\t\t\tcpas_core->axi_port[mnoc_idx].bus_client\n\t\t\t\t\t.common_data.dst_id = dst_args.args[0];",
     "\t\t\t\t\tcpas_core->axi_port[mnoc_idx].bus_client\n\t\t\t\t\t.common_data.dst_id = dst_args.args[0];\n\t\t\t\t\tcpas_core->axi_port[mnoc_idx].bus_client\n\t\t\t\t\t.common_data.icc_np = mnoc_node;"),
    ("\t\tcpas_core->ahb_bus_client.common_data.dst_id = dst_args.args[0];",
     "\t\tcpas_core->ahb_bus_client.common_data.dst_id = dst_args.args[0];\n\t\tcpas_core->ahb_bus_client.common_data.icc_np = of_node;"),
])
edit("cam_utils/cam_soc_icc.c", [("bus_client_data->icc_data = icc_get(&pdev->dev,\n\t\tbus_client->common_data->src_id,\n\t\tbus_client->common_data->dst_id);",
     "bus_client_data->icc_data = cam_icc_get_np(\n\t\tbus_client->common_data->icc_np);")])
# SMMU: 5.4's qcom IOMMU domain knobs have no 6.12 equivalent; the
# discard region is kept out with iommu-addresses in the device tree
edit("cam_smmu/cam_smmu_api.c", [
    ("\t\tiommu_dma_enable_best_fit_algo(dev);\n", ""),
    ("\t\tif (cb->discard_iova_start)\n\t\t\tiommu_dma_reserve_iova(dev, cb->discard_iova_start,\n\t\t\t\tcb->discard_iova_len);\n", ""),
])
f = os.path.join(root, "cam_smmu/cam_smmu_api.c")
t = open(f).read()
t = re.sub(r"iommu_domain_set_attr\(cb->domain,\s*DOMAIN_ATTR_FAULT_MODEL_\w+,\s*&\w+\)", "0", t)
open(f, "w").write(t)
# camera_main uses the DMA_BUF namespace for the heaps
f = os.path.join(root, "camera_main.c")
open(f, "a").write("\nMODULE_IMPORT_NS(DMA_BUF);\n")

# DT devices have no IRQ resources on 6.12: look the IRQ up by name and
# wrap it in a resource, since the driver uses irq_line->start
edit("cam_utils/cam_soc_util.c", [("""		soc_info->irq_line =
			platform_get_resource_byname(soc_info->pdev,
			IORESOURCE_IRQ, soc_info->irq_name);
		if (!soc_info->irq_line) {""", """		int irq = platform_get_irq_byname(soc_info->pdev,
			soc_info->irq_name);

		soc_info->irq_line = irq < 0 ? NULL :
			devm_kzalloc(&soc_info->pdev->dev,
				sizeof(*soc_info->irq_line), GFP_KERNEL);
		if (soc_info->irq_line) {
			soc_info->irq_line->start = irq;
			soc_info->irq_line->end = irq;
			soc_info->irq_line->flags = IORESOURCE_IRQ;
			soc_info->irq_line->name = soc_info->irq_name;
		}
		if (!soc_info->irq_line) {""")])

# say why the secondary heap mapping fails (iommu_map_sg() returns ssize_t)
edit("cam_smmu/cam_smmu_api.c", [("""	if (size != sec_heap_iova_len) {
		CAM_ERR(CAM_SMMU, "IOMMU mapping failed");""", """	if (size != sec_heap_iova_len) {
		CAM_ERR(CAM_SMMU, "IOMMU mapping failed: %zd of %zu at 0x%lx, %u entries, buf %zu",
			(ssize_t)size, (size_t)sec_heap_iova_len, (unsigned long)sec_heap_iova,
			secheap_buf->table->nents, secheap_buf->buf->size);""")])

# iommu_map_sg() walks the CPU scatterlist: pass orig_nents, not the count
# dma_map_sg() merged it to (the 1 MB secheap mapped only its first 64 KB)
f = os.path.join(root, "cam_smmu/cam_smmu_api.c")
t = open(f).read()
out, i = [], 0
for m in re.finditer(r"\biommu_map_sg\(", t):
    if m.start() < i:
        continue
    j, depth = m.end(), 1
    while depth:
        depth += {"(": 1, ")": -1}.get(t[j], 0)
        j += 1
    out.append(t[i:m.start()] + t[m.start():j].replace("table->nents", "table->orig_nents"))
    i = j
out.append(t[i:])
open(f, "w").write("".join(out))
print("interconnect, smmu, irq, sg edits done")

