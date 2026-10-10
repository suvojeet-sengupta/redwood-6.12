// SPDX-License-Identifier: GPL-2.0
/*
 * Print the kernel log on the framebuffer the bootloader left on screen.
 *
 * For bringing up a kernel before its display driver works: the display
 * keeps scanning out the splash buffer, so text drawn into it shows up.
 */
#include <linux/console.h>
#include <linux/font.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/spinlock.h>

static ulong base = 0xe1000000;
module_param(base, ulong, 0444);
static uint width = 1080;
module_param(width, uint, 0444);
static uint height = 2400;
module_param(height, uint, 0444);
static uint bpp = 3;
module_param(bpp, uint, 0444);
static uint stride;
module_param(stride, uint, 0444);
static uint scale = 2;
module_param(scale, uint, 0444);

static u8 *fb;
static uint cols, rows, cx, cy;
static DEFINE_SPINLOCK(lock);

static void fill(uint x0, uint y0, uint w, uint h, u32 rgb)
{
	uint x, y;

	for (y = y0; y < y0 + h && y < height; y++) {
		u8 *p = fb + y * stride + x0 * bpp;

		for (x = x0; x < x0 + w && x < width; x++, p += bpp) {
			p[0] = rgb;
			p[1] = rgb >> 8;
			p[2] = rgb >> 16;
			if (bpp == 4)
				p[3] = 0xff;
		}
	}
}

static void draw_char(uint col, uint row, u8 ch, u32 fg)
{
	const u8 *glyph = (const u8 *)font_vga_8x16.data + ch * 16;
	uint x, y;

	for (y = 0; y < 16; y++)
		for (x = 0; x < 8; x++)
			fill((col * 8 + x) * scale, (row * 16 + y) * scale,
			     scale, scale,
			     glyph[y] & (0x80 >> x) ? fg : 0);
}

static void next_line(void)
{
	cx = 0;
	cy = (cy + 1) % rows;
	/* Wrap instead of scrolling, and clear the line we land on plus a
	 * marker line under it so the newest text is easy to find. */
	fill(0, cy * 16 * scale, width, 16 * scale, 0);
	fill(0, ((cy + 1) % rows) * 16 * scale, width, 16 * scale, 0x202060);
}

static void splash_write(struct console *con, const char *s, unsigned int n)
{
	unsigned long flags;

	spin_lock_irqsave(&lock, flags);
	while (n--) {
		u8 ch = *s++;

		if (ch == '\n') {
			next_line();
			continue;
		}
		if (cx >= cols)
			next_line();
		draw_char(cx++, cy, ch, 0xffffff);
	}
	spin_unlock_irqrestore(&lock, flags);
}

static struct console splash_console = {
	.name	= "splash",
	.write	= splash_write,
	.flags	= CON_PRINTBUFFER | CON_ENABLED | CON_ANYTIME,
	.index	= -1,
};

/*
 * The display pipes (SSPPs) the bootloader may have used, and the registers
 * that say where and how each one fetches its picture.
 */
static const phys_addr_t sspp_regs[] = { 0x0ae05000, 0x0ae25000, 0x0ae27000, 0x0ae29000 };
#define SSPP_SRC_SIZE		0x00
#define SSPP_SRC_ADDR0		0x14
#define SSPP_SRC_YSTRIDE0	0x24
#define SSPP_SRC_FORMAT		0x30

static bool find_scanout(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sspp_regs); i++) {
		void __iomem *r = ioremap(sspp_regs[i], 0x100);
		u32 size, addr, ystride, fmt;

		if (!r)
			continue;
		size = readl_relaxed(r + SSPP_SRC_SIZE);
		addr = readl_relaxed(r + SSPP_SRC_ADDR0);
		ystride = readl_relaxed(r + SSPP_SRC_YSTRIDE0) & 0xffff;
		fmt = readl_relaxed(r + SSPP_SRC_FORMAT);
		iounmap(r);
		pr_info("splashcon: sspp%d addr=%#x size=%ux%u stride=%u format=%#x\n",
			i, addr, size & 0xffff, size >> 16, ystride, fmt);
		if (!addr || !ystride || !(size & 0xffff))
			continue;
		base = addr;
		width = size & 0xffff;
		height = size >> 16;
		stride = ystride;
		bpp = ((fmt >> 9) & 3) + 1;
		if (bpp < 3)
			bpp = stride / width;
		return true;
	}
	return false;
}

static int __init splashcon_init(void)
{
	if (!find_scanout())
		return -ENODEV;
	fb = memremap(base, (size_t)stride * height, MEMREMAP_WC);
	if (!fb)
		return -ENOMEM;
	cols = width / (8 * scale);
	rows = height / (16 * scale);
	fill(0, 0, width, height, 0xff00ff);
	register_console(&splash_console);
	return 0;
}

static void __exit splashcon_exit(void)
{
	unregister_console(&splash_console);
	memunmap(fb);
}

module_init(splashcon_init);
module_exit(splashcon_exit);
MODULE_DESCRIPTION("Kernel log on the bootloader's splash framebuffer");
MODULE_LICENSE("GPL");
