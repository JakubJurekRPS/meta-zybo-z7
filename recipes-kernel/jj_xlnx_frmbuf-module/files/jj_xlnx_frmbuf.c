// SPDX-License-Identifier: GPL-2.0-only
/*
 * Simplest possible simple frame-buffer driver, as a platform device
 *
 * Copyright (c) 2013, Stephen Warren
 *
 * Based on q40fb.c, which was:
 * Copyright (C) 2001 Richard Zidlicky <rz@linux-m68k.org>
 *
 * Also based on offb.c, which was:
 * Copyright (C) 1997 Geert Uytterhoeven
 * Copyright (C) 1996 Paul Mackerras
 */

#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_clk.h>
#include <linux/of_platform.h>
#include <linux/parser.h>
#include <linux/regulator/consumer.h>
#include <drm/drm_fourcc.h>
#include <linux/kernel.h>
#include <linux/dma/xilinx_frmbuf.h>
#include <linux/videodev2.h>

/* format array, use it to initialize a "struct simplefb_format" array */
#define JJXLNXFB_FORMATS \
{ \
	{ "r5g6b5", 16, {11, 5}, {5, 6}, {0, 5}, {0, 0}, DRM_FORMAT_RGB565 }, \
	{ "x1r5g5b5", 16, {10, 5}, {5, 5}, {0, 5}, {0, 0}, DRM_FORMAT_XRGB1555 }, \
	{ "a1r5g5b5", 16, {10, 5}, {5, 5}, {0, 5}, {15, 1}, DRM_FORMAT_ARGB1555 }, \
	{ "rgb888", 24, {16, 8}, {8, 8}, {0, 8}, {0, 0}, DRM_FORMAT_RGB888 }, \
	{ "x8r8g8b8", 32, {16, 8}, {8, 8}, {0, 8}, {0, 0}, DRM_FORMAT_XRGB8888 }, \
	{ "a8r8g8b8", 32, {16, 8}, {8, 8}, {0, 8}, {24, 8}, DRM_FORMAT_ARGB8888 }, \
	{ "a8b8g8r8", 32, {0, 8}, {8, 8}, {16, 8}, {24, 8}, DRM_FORMAT_ABGR8888 }, \
	{ "x2r10g10b10", 32, {20, 10}, {10, 10}, {0, 10}, {0, 0}, DRM_FORMAT_XRGB2101010 }, \
	{ "a2r10g10b10", 32, {20, 10}, {10, 10}, {0, 10}, {30, 2}, DRM_FORMAT_ARGB2101010 }, \
}

/*
 * Data-Format for Simple-Framebuffers
 * @name: unique 0-terminated name that can be used to identify the mode
 * @red,green,blue: Offsets and sizes of the single RGB parts
 * @transp: Offset and size of the alpha bits. length=0 means no alpha
 * @fourcc: 32bit DRM four-CC code (see drm_fourcc.h)
 */
struct jjxlnxfb_format {
	const char *name;
	u32 bits_per_pixel;
	struct fb_bitfield red;
	struct fb_bitfield green;
	struct fb_bitfield blue;
	struct fb_bitfield transp;
	u32 fourcc;
};

/*
 * Simple-Framebuffer description
 * If the arch-boot code creates simple-framebuffers without DT support, it
 * can pass the width, height, and format via this platform-data object.
 * The framebuffer location must be given as IORESOURCE_MEM resource.
 * @format must be a format as described in "struct simplefb_format" above.
 */
struct jjxlnxfb_platform_data {
	u32 width;
	u32 height;
	const char *format;
	phandle phandle_fb;
};

static const struct fb_fix_screeninfo jjxlnxfb_fix = {
	.id		= "simple",
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_TRUECOLOR,
	.accel		= FB_ACCEL_NONE,
};

static const struct fb_var_screeninfo jjxlnxfb_var = {
	.height		= -1,
	.width		= -1,
	.activate	= FB_ACTIVATE_NOW,
	.vmode		= FB_VMODE_NONINTERLACED,
};

#define PSEUDO_PALETTE_SIZE 256

static int jjxlnxfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			      u_int transp, struct fb_info *info)
{
	u32 *pal = info->pseudo_palette;
	u32 cr = red >> (16 - info->var.red.length);
	u32 cg = green >> (16 - info->var.green.length);
	u32 cb = blue >> (16 - info->var.blue.length);
	u32 value;

	if (regno >= PSEUDO_PALETTE_SIZE)
		return -EINVAL;

	value = (cr << info->var.red.offset) |
		(cg << info->var.green.offset) |
		(cb << info->var.blue.offset);
	if (info->var.transp.length > 0) {
		u32 mask = (1 << info->var.transp.length) - 1;
		mask <<= info->var.transp.offset;
		value |= mask;
	}
	pal[regno] = value;

	return 0;
}

struct jjxlnxfb_par;
static void jjxlnxfb_clocks_destroy(struct jjxlnxfb_par *par);
static void jjxlnxfb_regulators_destroy(struct jjxlnxfb_par *par);

static void jjxlnxfb_destroy(struct fb_info *info)
{
	jjxlnxfb_regulators_destroy(info->par);
	jjxlnxfb_clocks_destroy(info->par);
	if (info->screen_base)
		iounmap(info->screen_base);
}

static struct fb_ops jjxlnxfb_ops = {
	.owner		= THIS_MODULE,
	.fb_destroy	= jjxlnxfb_destroy,
	.fb_setcolreg	= jjxlnxfb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static struct jjxlnxfb_format jjxlnxfb_formats[] = JJXLNXFB_FORMATS;

struct jjxlnxfb_params {
	u32 width;
	u32 height;
	struct jjxlnxfb_format *format;
	phandle phandle_fb;
};

static int jjxlnxfb_parse_dt(struct platform_device *pdev,
			   struct jjxlnxfb_params *params)
{
	struct device_node *np = pdev->dev.of_node;
	int ret;
	const char *format;
	int i;

	ret = of_property_read_u32(np, "width", &params->width);
	if (ret) {
		dev_err(&pdev->dev, "Can't parse width property\n");
		return ret;
	}

	ret = of_property_read_u32(np, "height", &params->height);
	if (ret) {
		dev_err(&pdev->dev, "Can't parse height property\n");
		return ret;
	}

	ret = of_property_read_string(np, "format", &format);
	if (ret) {
		dev_err(&pdev->dev, "Can't parse format property\n");
		return ret;
	}

	ret = of_property_read_u32(np, "phandle_fb", &params->phandle_fb);
	if (ret) {
		dev_err(&pdev->dev, "Can't parse phandle_fb property\n");
		return ret;
	}

	params->format = NULL;
	for (i = 0; i < ARRAY_SIZE(jjxlnxfb_formats); i++) {
		if (strcmp(format, jjxlnxfb_formats[i].name))
			continue;
		params->format = &jjxlnxfb_formats[i];
		break;
	}
	if (!params->format) {
		dev_err(&pdev->dev, "Invalid format value\n");
		return -EINVAL;
	}

	return 0;
}

static int jjxlnxfb_parse_pd(struct platform_device *pdev,
			     struct jjxlnxfb_params *params)
{
	struct jjxlnxfb_platform_data *pd = dev_get_platdata(&pdev->dev);
	int i;

	params->width = pd->width;
	params->height = pd->height;
	params->phandle_fb = pd->phandle_fb;
	params->format = NULL;
	for (i = 0; i < ARRAY_SIZE(jjxlnxfb_formats); i++) {
		if (strcmp(pd->format, jjxlnxfb_formats[i].name))
			continue;

		params->format = &jjxlnxfb_formats[i];
		break;
	}

	if (!params->format) {
		dev_err(&pdev->dev, "Invalid format value\n");
		return -EINVAL;
	}

	return 0;
}

struct jjxlnxfb_par {
	u32 palette[PSEUDO_PALETTE_SIZE];
	struct dma_chan * dma_fb_chan;
#if defined CONFIG_OF && defined CONFIG_COMMON_CLK
	bool clks_enabled;
	unsigned int clk_count;
	struct clk **clks;
#endif
#if defined CONFIG_OF && defined CONFIG_REGULATOR
	bool regulators_enabled;
	u32 regulator_count;
	struct regulator **regulators;
#endif
};

#if defined CONFIG_OF && defined CONFIG_COMMON_CLK
/*
 * Clock handling code.
 *
 * Here we handle the clocks property of our "jj-xlnx-framebuffer" dt node.
 * This is necessary so that we can make sure that any clocks needed by
 * the display engine that the bootloader set up for us (and for which it
 * provided a jjxlnxfb dt node), stay up, for the life of the jjxlnxfb
 * driver.
 *
 * When the driver unloads, we cleanly disable, and then release the clocks.
 *
 * We only complain about errors here, no action is taken as the most likely
 * error can only happen due to a mismatch between the bootloader which set
 * up jjxlnxfb, and the clock definitions in the device tree. Chances are
 * that there are no adverse effects, and if there are, a clean teardown of
 * the fb probe will not help us much either. So just complain and carry on,
 * and hope that the user actually gets a working fb at the end of things.
 */
static int jjxlnxfb_clocks_get(struct jjxlnxfb_par *par,
			       struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct clk *clock;
	int i;

	if (dev_get_platdata(&pdev->dev) || !np)
		return 0;

	par->clk_count = of_clk_get_parent_count(np);
	if (!par->clk_count)
		return 0;

	par->clks = kcalloc(par->clk_count, sizeof(struct clk *), GFP_KERNEL);
	if (!par->clks)
		return -ENOMEM;

	for (i = 0; i < par->clk_count; i++) {
		clock = of_clk_get(np, i);
		if (IS_ERR(clock)) {
			if (PTR_ERR(clock) == -EPROBE_DEFER) {
				while (--i >= 0) {
					if (par->clks[i])
						clk_put(par->clks[i]);
				}
				kfree(par->clks);
				return -EPROBE_DEFER;
			}
			dev_err(&pdev->dev, "%s: clock %d not found: %ld\n",
				__func__, i, PTR_ERR(clock));
			continue;
		}
		par->clks[i] = clock;
	}

	return 0;
}

static void jjxlnxfb_clocks_enable(struct jjxlnxfb_par *par,
				   struct platform_device *pdev)
{
	int i, ret;

	for (i = 0; i < par->clk_count; i++) {
		if (par->clks[i]) {
			ret = clk_prepare_enable(par->clks[i]);
			if (ret) {
				dev_err(&pdev->dev,
					"%s: failed to enable clock %d: %d\n",
					__func__, i, ret);
				clk_put(par->clks[i]);
				par->clks[i] = NULL;
			}
		}
	}
	par->clks_enabled = true;
}

static void jjxlnxfb_clocks_destroy(struct jjxlnxfb_par *par)
{
	int i;

	if (!par->clks)
		return;

	for (i = 0; i < par->clk_count; i++) {
		if (par->clks[i]) {
			if (par->clks_enabled)
				clk_disable_unprepare(par->clks[i]);
			clk_put(par->clks[i]);
		}
	}

	kfree(par->clks);
}
#else
static int jjxlnxfb_clocks_get(struct jjxlnxfb_par *par,
	struct platform_device *pdev) { return 0; }
static void jjxlnxfb_clocks_enable(struct jjxlnxfb_par *par,
	struct platform_device *pdev) { }
static void jjxlnxfb_clocks_destroy(struct jjxlnxfb_par *par) { }
#endif

#if defined CONFIG_OF && defined CONFIG_REGULATOR

#define SUPPLY_SUFFIX "-supply"

/*
 * Regulator handling code.
 *
 * Here we handle the num-supplies and vin*-supply properties of our
 * "jj-xlnx-framebuffer" dt node. This is necessary so that we can make sure
 * that any regulators needed by the display hardware that the bootloader
 * set up for us (and for which it provided a jjxlnxfb dt node), stay up,
 * for the life of the jjxlnxfb driver.
 *
 * When the driver unloads, we cleanly disable, and then release the
 * regulators.
 *
 * We only complain about errors here, no action is taken as the most likely
 * error can only happen due to a mismatch between the bootloader which set
 * up jjxlnxfb, and the regulator definitions in the device tree. Chances are
 * that there are no adverse effects, and if there are, a clean teardown of
 * the fb probe will not help us much either. So just complain and carry on,
 * and hope that the user actually gets a working fb at the end of things.
 */
static int jjxlnxfb_regulators_get(struct jjxlnxfb_par *par,
				   struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct property *prop;
	struct regulator *regulator;
	const char *p;
	int count = 0, i = 0;

	if (dev_get_platdata(&pdev->dev) || !np)
		return 0;

	/* Count the number of regulator supplies */
	for_each_property_of_node(np, prop) {
		p = strstr(prop->name, SUPPLY_SUFFIX);
		if (p && p != prop->name)
			count++;
	}

	if (!count)
		return 0;

	par->regulators = devm_kcalloc(&pdev->dev, count,
				       sizeof(struct regulator *), GFP_KERNEL);
	if (!par->regulators)
		return -ENOMEM;

	/* Get all the regulators */
	for_each_property_of_node(np, prop) {
		char name[32]; /* 32 is max size of property name */

		p = strstr(prop->name, SUPPLY_SUFFIX);
		if (!p || p == prop->name)
			continue;

		strlcpy(name, prop->name,
			strlen(prop->name) - strlen(SUPPLY_SUFFIX) + 1);
		regulator = devm_regulator_get_optional(&pdev->dev, name);
		if (IS_ERR(regulator)) {
			if (PTR_ERR(regulator) == -EPROBE_DEFER)
				return -EPROBE_DEFER;
			dev_err(&pdev->dev, "regulator %s not found: %ld\n",
				name, PTR_ERR(regulator));
			continue;
		}
		par->regulators[i++] = regulator;
	}
	par->regulator_count = i;

	return 0;
}

static void jjxlnxfb_regulators_enable(struct jjxlnxfb_par *par,
				       struct platform_device *pdev)
{
	int i, ret;

	/* Enable all the regulators */
	for (i = 0; i < par->regulator_count; i++) {
		ret = regulator_enable(par->regulators[i]);
		if (ret) {
			dev_err(&pdev->dev,
				"failed to enable regulator %d: %d\n",
				i, ret);
			devm_regulator_put(par->regulators[i]);
			par->regulators[i] = NULL;
		}
	}
	par->regulators_enabled = true;
}

static void jjxlnxfb_regulators_destroy(struct jjxlnxfb_par *par)
{
	int i;
	if (!par->regulators || !par->regulators_enabled)
		return;

	for (i = 0; i < par->regulator_count; i++)
		if (par->regulators[i])
			regulator_disable(par->regulators[i]);
}
#else
static int jjxlnxfb_regulators_get(struct jjxlnxfb_par *par,
	struct platform_device *pdev) { return 0; }
static void jjxlnxfb_regulators_enable(struct jjxlnxfb_par *par,
	struct platform_device *pdev) { }
static void jjxlnxfb_regulators_destroy(struct jjxlnxfb_par *par) { }
#endif

static int jjxlnxfb_probe(struct platform_device *pdev)
{
	int ret;
	struct jjxlnxfb_params params;
	struct fb_info *info;
	struct jjxlnxfb_par *par;
	void *fbmem_virt;
	struct device_node *dev_fb_dma;
	dma_cap_mask_t dma_cap_mask;
	struct dma_interleaved_template dma_tmplt;
	dma_addr_t addr;
	struct dma_async_tx_descriptor * tx_desc;
	dma_cookie_t cookie;

	if (fb_get_options("jjxlnxfb", NULL))
		return -ENODEV;

	ret = -ENODEV;
	if (dev_get_platdata(&pdev->dev))
		ret = jjxlnxfb_parse_pd(pdev, &params);
	else if (pdev->dev.of_node)
		ret = jjxlnxfb_parse_dt(pdev, &params);

	if (ret)
		return ret;

	info = framebuffer_alloc(sizeof(struct jjxlnxfb_par), &pdev->dev);
	if (!info)
		return -ENOMEM;
	platform_set_drvdata(pdev, info);

	info->var = jjxlnxfb_var;
	info->var.xres = params.width;
	info->var.yres = params.height;
	info->var.xres_virtual = params.width;
	info->var.yres_virtual = params.height;
	info->var.bits_per_pixel = params.format->bits_per_pixel;
	info->var.red = params.format->red;
	info->var.green = params.format->green;
	info->var.blue = params.format->blue;
	info->var.transp = params.format->transp;

	info->fix = jjxlnxfb_fix;
	info->fix.line_length = (info->var.xres * 
		(info->var.bits_per_pixel >> 3));
	info->fix.smem_len = info->fix.line_length * info->var.yres;

	// alloc memory for the frame
	fbmem_virt = dma_alloc_coherent(&pdev->dev,
				info->fix.smem_len,
				(void *)&info->fix.smem_start,
				GFP_KERNEL);
	if (!fbmem_virt) {
		dev_err(&pdev->dev,
			"Unable to allocate %d Bytes fb memory\n",
			info->fix.smem_len);
		goto error_fb_release;
	}

	info->apertures = alloc_apertures(1);
	if (!info->apertures) {
		ret = -ENOMEM;
		goto err_dma_alloc;
	}
	info->apertures->ranges[0].base = info->fix.smem_start;
	info->apertures->ranges[0].size = info->fix.smem_len;

	info->fbops = &jjxlnxfb_ops;
	info->flags = FBINFO_DEFAULT | FBINFO_MISC_FIRMWARE;
	info->screen_base = (char *)fbmem_virt;

	ret = fb_alloc_cmap(&info->cmap, PSEUDO_PALETTE_SIZE, 0);
	if (ret < 0)
		goto error_apertures_release;

	par = info->par;
	info->pseudo_palette = par->palette;

	ret = jjxlnxfb_clocks_get(par, pdev);
	if (ret < 0)
		goto error_free_cmap;

	ret = jjxlnxfb_regulators_get(par, pdev);
	if (ret < 0)
		goto error_clocks;

	jjxlnxfb_clocks_enable(par, pdev);
	jjxlnxfb_regulators_enable(par, pdev);

	dev_info(&pdev->dev, "framebuffer at 0x%lx, 0x%x bytes, mapped to 0x%p\n",
			     info->fix.smem_start, info->fix.smem_len,
			     info->screen_base);
	dev_info(&pdev->dev, "format=%s, mode=%dx%dx%d, linelength=%d\n",
			     params.format->name,
			     info->var.xres, info->var.yres,
			     info->var.bits_per_pixel, info->fix.line_length);

// JJTD: start xilinx dma framebuffer

	dev_fb_dma = of_find_node_by_phandle(params.phandle_fb);
	if(NULL == dev_fb_dma)
	{
		dev_err(&pdev->dev,
			"Unable to find xlnx framebuffer node");
		goto error_regulators;
	}
	dma_cap_zero(dma_cap_mask); /* Clear the mask */
	dma_cap_set(DMA_SLAVE, dma_cap_mask); /* Set the capability */
	dma_cap_set(DMA_PRIVATE, dma_cap_mask); /* Set the capability */

	par->dma_fb_chan = __dma_request_channel(&dma_cap_mask, NULL, NULL, dev_fb_dma);
	if(NULL == par->dma_fb_chan)
	{
		dev_err(&pdev->dev,
			"Unable to obtain dma channel");
		goto error_regulators;
	}
	xilinx_xdma_v4l2_config(par->dma_fb_chan, V4L2_PIX_FMT_BGRX32);
	xilinx_xdma_set_mode(par->dma_fb_chan, AUTO_RESTART);

	dma_tmplt.dir = DMA_MEM_TO_DEV;
	dma_tmplt.src_sgl = true;
	dma_tmplt.dst_sgl = false;
	dma_tmplt.src_start = (dma_addr_t)info->fix.smem_start;
	dma_tmplt.frame_size = 1;
	dma_tmplt.numf = info->var.yres;
	dma_tmplt.sgl[0].size = info->fix.line_length;
	dma_tmplt.sgl[0].icg = 0;

	tx_desc = dmaengine_prep_interleaved_dma(par->dma_fb_chan, &dma_tmplt, NULL);

	if(NULL == tx_desc)
	{
		dev_err(&pdev->dev,
			"Unable to prep desc");
		goto error_regulators;
	}
	cookie = dmaengine_submit(tx_desc);
	if (dma_submit_error(cookie)){
		dev_err(&pdev->dev, "Failed to submit DMA\n");		
		goto error_regulators;
	};

	dma_async_issue_pending(par->dma_fb_chan);

	ret = register_framebuffer(info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to register jjxlnxfb: %d\n", ret);
		goto error_regulators;
	}

	dev_info(&pdev->dev, "fb%d: jjxlnxfb registered!\n", info->node);

	return 0;

error_regulators:
	jjxlnxfb_regulators_destroy(par);
error_clocks:
	jjxlnxfb_clocks_destroy(par);
error_free_cmap:
	fb_dealloc_cmap(&info->cmap);
error_apertures_release:
	kfree(info->apertures);
err_dma_alloc:
	dma_free_coherent(&pdev->dev, info->fix.smem_len, fbmem_virt,
			  info->fix.smem_start);
error_fb_release:
	framebuffer_release(info);
	return ret;
}

static int jjxlnxfb_remove(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);

	unregister_framebuffer(info);
	framebuffer_release(info);

	return 0;
}

static const struct of_device_id jjxlnxfb_of_match[] = {
	{ .compatible = "jj-xlnx-framebuffer", },
	{ },
};
MODULE_DEVICE_TABLE(of, jjxlnxfb_of_match);

static struct platform_driver jjxlnxfb_driver = {
	.driver = {
		.name = "jj-xlnx-framebuffer",
		.of_match_table = jjxlnxfb_of_match,
	},
	.probe = jjxlnxfb_probe,
	.remove = jjxlnxfb_remove,
};

static int __init jjxlnxfb_init(void)
{
	int ret;
	struct device_node *np;

	ret = platform_driver_register(&jjxlnxfb_driver);
	if (ret)
		return ret;

	if (IS_ENABLED(CONFIG_OF_ADDRESS) && of_chosen) {
		for_each_child_of_node(of_chosen, np) {
			if (of_device_is_compatible(np, "jj-xlnx-framebuffer"))
				of_platform_device_create(np, NULL, NULL);
		}
	}

	return 0;
}

static void __exit jjxlnxfb_exit(void)
{
	platform_driver_unregister(&jjxlnxfb_driver);
}

module_init(jjxlnxfb_init);
module_exit(jjxlnxfb_exit);
MODULE_AUTHOR("Stephen Warren <swarren@wwwdotorg.org>");
MODULE_DESCRIPTION("Simple framebuffer driver");
MODULE_LICENSE("GPL v2");
