// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct visionox_rm69299 {
	struct drm_panel panel;
	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset_gpio;
	struct mipi_dsi_device *dsi;
	const struct drm_display_mode *mode;
	bool prepared;
	bool enabled;
};

static const struct drm_display_mode visionox_rm69299_1080x2248_60hz = {
	.name = "1080x2248",
	.clock = 158695,
	.hdisplay = 1080,
	.hsync_start = 1080 + 26,
	.hsync_end = 1080 + 26 + 2,
	.htotal = 1080 + 26 + 2 + 36,
	.vdisplay = 2248,
	.vsync_start = 2248 + 56,
	.vsync_end = 2248 + 56 + 4,
	.vtotal = 2248 + 56 + 4 + 4,
	.flags = 0,
};

static const struct drm_display_mode visionox_rm69299_1080x2160_60hz = {
	.name = "Visionox 1080x2160@60Hz",
	.clock = 158695,
	.hdisplay = 1080,
	.hsync_start = 1080 + 26,
	.hsync_end = 1080 + 26 + 2,
	.htotal = 1080 + 26 + 2 + 36,
	.vdisplay = 2160,
	.vsync_start = 2160 + 8,
	.vsync_end = 2160 + 8 + 4,
	.vtotal = 2160 + 8 + 4 + 4,
	.flags = 0,
	.width_mm = 74,
	.height_mm = 131,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

#define dsi_dcs_write_seq(dsi, seq...) do {				\
		static const u8 d[] = { seq };				\
		int ret;						\
		ret = mipi_dsi_dcs_write_buffer(dsi, d, ARRAY_SIZE(d));	\
		if (ret < 0)						\
			return ret;					\
	} while (0)

static inline struct visionox_rm69299 *panel_to_ctx(struct drm_panel *panel)
{
	return container_of(panel, struct visionox_rm69299, panel);
}

static int visionox_rm69299_power_on(struct visionox_rm69299 *ctx)
{
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	/*
	 * Reset sequence of visionox panel requires the panel to be
	 * out of reset for 10ms, followed by being held in reset
	 * for 10ms and then out again
	 */
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(10000, 20000);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(10000, 20000);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(10000, 20000);

	return 0;
}

static int visionox_rm69299_power_off(struct visionox_rm69299 *ctx)
{
	gpiod_set_value(ctx->reset_gpio, 0);

	return regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
}

static int visionox_rm69299_unprepare(struct drm_panel *panel)
{
	struct visionox_rm69299 *ctx = panel_to_ctx(panel);
	int ret;

	ctx->dsi->mode_flags = 0;

	ret = mipi_dsi_dcs_write(ctx->dsi, MIPI_DCS_SET_DISPLAY_OFF, NULL, 0);
	if (ret < 0)
		dev_err(ctx->panel.dev, "set_display_off cmd failed ret = %d\n", ret);

	/* 120ms delay required here as per DCS spec */
	msleep(120);

	ret = mipi_dsi_dcs_write(ctx->dsi, MIPI_DCS_ENTER_SLEEP_MODE, NULL, 0);
	if (ret < 0) {
		dev_err(ctx->panel.dev, "enter_sleep cmd failed ret = %d\n", ret);
	}

	ret = visionox_rm69299_power_off(ctx);

	ctx->prepared = false;
	return ret;
}

static int visionox_rm69299_prepare(struct drm_panel *panel)
{
	struct visionox_rm69299 *ctx = panel_to_ctx(panel);
	int ret;

	if (ctx->prepared)
		return 0;

	ret = visionox_rm69299_power_on(ctx);
	if (ret < 0)
		return ret;

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	if (ctx->mode == &visionox_rm69299_1080x2160_60hz) {
		dsi_dcs_write_seq(ctx->dsi, 0xFE, 0x40);
		dsi_dcs_write_seq(ctx->dsi, 0x05, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x06, 0x08);
		dsi_dcs_write_seq(ctx->dsi, 0x08, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x09, 0x08);
		dsi_dcs_write_seq(ctx->dsi, 0x0A, 0x07);
		dsi_dcs_write_seq(ctx->dsi, 0x0B, 0xCC);
		dsi_dcs_write_seq(ctx->dsi, 0x0C, 0x07);
		dsi_dcs_write_seq(ctx->dsi, 0x0D, 0x90);
		dsi_dcs_write_seq(ctx->dsi, 0x0F, 0x87);
		dsi_dcs_write_seq(ctx->dsi, 0x20, 0x8D);
		dsi_dcs_write_seq(ctx->dsi, 0x21, 0x8D);
		dsi_dcs_write_seq(ctx->dsi, 0x24, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0x26, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0x28, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0x2A, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0x2D, 0x28);
		dsi_dcs_write_seq(ctx->dsi, 0x2F, 0x28);
		dsi_dcs_write_seq(ctx->dsi, 0x30, 0x32);
		dsi_dcs_write_seq(ctx->dsi, 0x31, 0x32);
		dsi_dcs_write_seq(ctx->dsi, 0x37, 0x80);
		dsi_dcs_write_seq(ctx->dsi, 0x38, 0x30);
		dsi_dcs_write_seq(ctx->dsi, 0x39, 0xA8);
		dsi_dcs_write_seq(ctx->dsi, 0x46, 0x48);
		dsi_dcs_write_seq(ctx->dsi, 0x47, 0x48);
		dsi_dcs_write_seq(ctx->dsi, 0x6B, 0x10);
		dsi_dcs_write_seq(ctx->dsi, 0x6F, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0x74, 0x2B);
		dsi_dcs_write_seq(ctx->dsi, 0x80, 0x1A);
		dsi_dcs_write_seq(ctx->dsi, 0xFE, 0x40);
		dsi_dcs_write_seq(ctx->dsi, 0x93, 0x10);
		dsi_dcs_write_seq(ctx->dsi, 0x16, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x85, 0x07);
		dsi_dcs_write_seq(ctx->dsi, 0x84, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x86, 0x0F);
		dsi_dcs_write_seq(ctx->dsi, 0x87, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0x8C, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x88, 0x2E);
		dsi_dcs_write_seq(ctx->dsi, 0x89, 0x2E);
		dsi_dcs_write_seq(ctx->dsi, 0x8B, 0x09);
		dsi_dcs_write_seq(ctx->dsi, 0x95, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x91, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x90, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x8D, 0xD0);
		dsi_dcs_write_seq(ctx->dsi, 0x8A, 0x03);
		dsi_dcs_write_seq(ctx->dsi, 0xFE, 0xA0);
		dsi_dcs_write_seq(ctx->dsi, 0x13, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x33, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x0B, 0x33);
		dsi_dcs_write_seq(ctx->dsi, 0x36, 0x1E);
		dsi_dcs_write_seq(ctx->dsi, 0x31, 0x88);
		dsi_dcs_write_seq(ctx->dsi, 0x32, 0x88);
		dsi_dcs_write_seq(ctx->dsi, 0x37, 0xF1);
		dsi_dcs_write_seq(ctx->dsi, 0xFE, 0x50);
		dsi_dcs_write_seq(ctx->dsi, 0x00, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x01, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x02, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x03, 0xE9);
		dsi_dcs_write_seq(ctx->dsi, 0x04, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x05, 0xF6);
		dsi_dcs_write_seq(ctx->dsi, 0x06, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x07, 0x2C);
		dsi_dcs_write_seq(ctx->dsi, 0x08, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x09, 0x62);
		dsi_dcs_write_seq(ctx->dsi, 0x0A, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x0B, 0x98);
		dsi_dcs_write_seq(ctx->dsi, 0x0C, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x0D, 0xBF);
		dsi_dcs_write_seq(ctx->dsi, 0x0E, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x0F, 0xF6);
		dsi_dcs_write_seq(ctx->dsi, 0x10, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0x11, 0x24);
		dsi_dcs_write_seq(ctx->dsi, 0x12, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0x13, 0x4E);
		dsi_dcs_write_seq(ctx->dsi, 0x14, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0x15, 0x70);
		dsi_dcs_write_seq(ctx->dsi, 0x16, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0x17, 0xAF);
		dsi_dcs_write_seq(ctx->dsi, 0x18, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0x19, 0xE2);
		dsi_dcs_write_seq(ctx->dsi, 0x1A, 0x03);
		dsi_dcs_write_seq(ctx->dsi, 0x1B, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0x1C, 0x03);
		dsi_dcs_write_seq(ctx->dsi, 0x1D, 0x52);
		dsi_dcs_write_seq(ctx->dsi, 0x1E, 0x03);
		dsi_dcs_write_seq(ctx->dsi, 0x1F, 0x82);
		dsi_dcs_write_seq(ctx->dsi, 0x20, 0x03);
		dsi_dcs_write_seq(ctx->dsi, 0x21, 0xB6);
		dsi_dcs_write_seq(ctx->dsi, 0x22, 0x03);
		dsi_dcs_write_seq(ctx->dsi, 0x23, 0xF0);
		dsi_dcs_write_seq(ctx->dsi, 0x24, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x25, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0x26, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x27, 0x37);
		dsi_dcs_write_seq(ctx->dsi, 0x28, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x29, 0x59);
		dsi_dcs_write_seq(ctx->dsi, 0x2A, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x2B, 0x68);
		dsi_dcs_write_seq(ctx->dsi, 0x30, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x31, 0x85);
		dsi_dcs_write_seq(ctx->dsi, 0x32, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x33, 0xA2);
		dsi_dcs_write_seq(ctx->dsi, 0x34, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x35, 0xBC);
		dsi_dcs_write_seq(ctx->dsi, 0x36, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x37, 0xD8);
		dsi_dcs_write_seq(ctx->dsi, 0x38, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x39, 0xF4);
		dsi_dcs_write_seq(ctx->dsi, 0x3A, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0x3B, 0x0E);
		dsi_dcs_write_seq(ctx->dsi, 0x40, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0x41, 0x13);
		dsi_dcs_write_seq(ctx->dsi, 0x42, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0x43, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0x44, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0x45, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0x46, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x47, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x48, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x49, 0x43);
		dsi_dcs_write_seq(ctx->dsi, 0x4A, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x4B, 0x4C);
		dsi_dcs_write_seq(ctx->dsi, 0x4C, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x4D, 0x6F);
		dsi_dcs_write_seq(ctx->dsi, 0x4E, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x4F, 0x92);
		dsi_dcs_write_seq(ctx->dsi, 0x50, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x51, 0xB5);
		dsi_dcs_write_seq(ctx->dsi, 0x52, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x53, 0xD4);
		dsi_dcs_write_seq(ctx->dsi, 0x58, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0x59, 0x06);
		dsi_dcs_write_seq(ctx->dsi, 0x5A, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0x5B, 0x33);
		dsi_dcs_write_seq(ctx->dsi, 0x5C, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0x5D, 0x59);
		dsi_dcs_write_seq(ctx->dsi, 0x5E, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0x5F, 0x7D);
		dsi_dcs_write_seq(ctx->dsi, 0x60, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0x61, 0xBD);
		dsi_dcs_write_seq(ctx->dsi, 0x62, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0x63, 0xF7);
		dsi_dcs_write_seq(ctx->dsi, 0x64, 0x03);
		dsi_dcs_write_seq(ctx->dsi, 0x65, 0x31);
		dsi_dcs_write_seq(ctx->dsi, 0x66, 0x03);
		dsi_dcs_write_seq(ctx->dsi, 0x67, 0x63);
		dsi_dcs_write_seq(ctx->dsi, 0x68, 0x03);
		dsi_dcs_write_seq(ctx->dsi, 0x69, 0x9D);
		dsi_dcs_write_seq(ctx->dsi, 0x6A, 0x03);
		dsi_dcs_write_seq(ctx->dsi, 0x6B, 0xD2);
		dsi_dcs_write_seq(ctx->dsi, 0x6C, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x6D, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0x6E, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x6F, 0x38);
		dsi_dcs_write_seq(ctx->dsi, 0x70, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x71, 0x51);
		dsi_dcs_write_seq(ctx->dsi, 0x72, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x73, 0x70);
		dsi_dcs_write_seq(ctx->dsi, 0x74, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x75, 0x85);
		dsi_dcs_write_seq(ctx->dsi, 0x76, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x77, 0xA1);
		dsi_dcs_write_seq(ctx->dsi, 0x78, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x79, 0xC0);
		dsi_dcs_write_seq(ctx->dsi, 0x7A, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x7B, 0xD8);
		dsi_dcs_write_seq(ctx->dsi, 0x7C, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x7D, 0xF2);
		dsi_dcs_write_seq(ctx->dsi, 0x7E, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0x7F, 0x10);
		dsi_dcs_write_seq(ctx->dsi, 0x80, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0x81, 0x21);
		dsi_dcs_write_seq(ctx->dsi, 0x82, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0x83, 0x2E);
		dsi_dcs_write_seq(ctx->dsi, 0x84, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0x85, 0x3A);
		dsi_dcs_write_seq(ctx->dsi, 0x86, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0x87, 0x3E);
		dsi_dcs_write_seq(ctx->dsi, 0x88, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x89, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x8A, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x8B, 0x86);
		dsi_dcs_write_seq(ctx->dsi, 0x8C, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x8D, 0x8F);
		dsi_dcs_write_seq(ctx->dsi, 0x8E, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x8F, 0xB3);
		dsi_dcs_write_seq(ctx->dsi, 0x90, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x91, 0xD7);
		dsi_dcs_write_seq(ctx->dsi, 0x92, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x93, 0xFB);
		dsi_dcs_write_seq(ctx->dsi, 0x94, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0x95, 0x18);
		dsi_dcs_write_seq(ctx->dsi, 0x96, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0x97, 0x4F);
		dsi_dcs_write_seq(ctx->dsi, 0x98, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0x99, 0x7E);
		dsi_dcs_write_seq(ctx->dsi, 0x9A, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0x9B, 0xA6);
		dsi_dcs_write_seq(ctx->dsi, 0x9C, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0x9D, 0xCF);
		dsi_dcs_write_seq(ctx->dsi, 0x9E, 0x03);
		dsi_dcs_write_seq(ctx->dsi, 0x9F, 0x14);
		dsi_dcs_write_seq(ctx->dsi, 0xA4, 0x03);
		dsi_dcs_write_seq(ctx->dsi, 0xA5, 0x52);
		dsi_dcs_write_seq(ctx->dsi, 0xA6, 0x03);
		dsi_dcs_write_seq(ctx->dsi, 0xA7, 0x93);
		dsi_dcs_write_seq(ctx->dsi, 0xAC, 0x03);
		dsi_dcs_write_seq(ctx->dsi, 0xAD, 0xCF);
		dsi_dcs_write_seq(ctx->dsi, 0xAE, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0xAF, 0x08);
		dsi_dcs_write_seq(ctx->dsi, 0xB0, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0xB1, 0x42);
		dsi_dcs_write_seq(ctx->dsi, 0xB2, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0xB3, 0x7F);
		dsi_dcs_write_seq(ctx->dsi, 0xB4, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0xB5, 0xB4);
		dsi_dcs_write_seq(ctx->dsi, 0xB6, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0xB7, 0xCC);
		dsi_dcs_write_seq(ctx->dsi, 0xB8, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0xB9, 0xF2);
		dsi_dcs_write_seq(ctx->dsi, 0xBA, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0xBB, 0x0C);
		dsi_dcs_write_seq(ctx->dsi, 0xBC, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0xBD, 0x26);
		dsi_dcs_write_seq(ctx->dsi, 0xBE, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0xBF, 0x4B);
		dsi_dcs_write_seq(ctx->dsi, 0xC0, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0xC1, 0x64);
		dsi_dcs_write_seq(ctx->dsi, 0xC2, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0xC3, 0x83);
		dsi_dcs_write_seq(ctx->dsi, 0xC4, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0xC5, 0xA1);
		dsi_dcs_write_seq(ctx->dsi, 0xC6, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0xC7, 0xBA);
		dsi_dcs_write_seq(ctx->dsi, 0xC8, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0xC9, 0xC4);
		dsi_dcs_write_seq(ctx->dsi, 0xCA, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0xCB, 0xD5);
		dsi_dcs_write_seq(ctx->dsi, 0xCC, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0xCD, 0xD5);
		dsi_dcs_write_seq(ctx->dsi, 0xCE, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0xCF, 0xCE);
		dsi_dcs_write_seq(ctx->dsi, 0xD0, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0xD1, 0xDB);
		dsi_dcs_write_seq(ctx->dsi, 0xD2, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0xD3, 0x32);
		dsi_dcs_write_seq(ctx->dsi, 0xD4, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0xD5, 0x3B);
		dsi_dcs_write_seq(ctx->dsi, 0xD6, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0xD7, 0x74);
		dsi_dcs_write_seq(ctx->dsi, 0xD8, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0xD9, 0x7D);
		dsi_dcs_write_seq(ctx->dsi, 0xFE, 0x60);
		dsi_dcs_write_seq(ctx->dsi, 0x00, 0xCC);
		dsi_dcs_write_seq(ctx->dsi, 0x01, 0x0F);
		dsi_dcs_write_seq(ctx->dsi, 0x02, 0xFF);
		dsi_dcs_write_seq(ctx->dsi, 0x03, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x04, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x05, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0x06, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x07, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x09, 0xC4);
		dsi_dcs_write_seq(ctx->dsi, 0x0A, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x0B, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x0C, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x0D, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x0E, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x0F, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x10, 0x71);
		dsi_dcs_write_seq(ctx->dsi, 0x12, 0xC4);
		dsi_dcs_write_seq(ctx->dsi, 0x13, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x14, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x15, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x16, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x17, 0x06);
		dsi_dcs_write_seq(ctx->dsi, 0x18, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x19, 0x71);
		dsi_dcs_write_seq(ctx->dsi, 0x1B, 0xC4);
		dsi_dcs_write_seq(ctx->dsi, 0x1C, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x1D, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0x1E, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x1F, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x20, 0x08);
		dsi_dcs_write_seq(ctx->dsi, 0x21, 0x66);
		dsi_dcs_write_seq(ctx->dsi, 0x22, 0xB4);
		dsi_dcs_write_seq(ctx->dsi, 0x24, 0xC4);
		dsi_dcs_write_seq(ctx->dsi, 0x25, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x26, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0x27, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x28, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x29, 0x07);
		dsi_dcs_write_seq(ctx->dsi, 0x2A, 0x66);
		dsi_dcs_write_seq(ctx->dsi, 0x2B, 0xB4);
		dsi_dcs_write_seq(ctx->dsi, 0x2F, 0xC4);
		dsi_dcs_write_seq(ctx->dsi, 0x30, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x31, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x32, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x33, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x34, 0x03);
		dsi_dcs_write_seq(ctx->dsi, 0x35, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x36, 0x71);
		dsi_dcs_write_seq(ctx->dsi, 0x38, 0xC4);
		dsi_dcs_write_seq(ctx->dsi, 0x39, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x3A, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x3B, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0x3D, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x3F, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0x40, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x41, 0x71);
		dsi_dcs_write_seq(ctx->dsi, 0x83, 0xCE);
		dsi_dcs_write_seq(ctx->dsi, 0x84, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0x85, 0x20);
		dsi_dcs_write_seq(ctx->dsi, 0x86, 0xDC);
		dsi_dcs_write_seq(ctx->dsi, 0x87, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x88, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0x89, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x8A, 0xBB);
		dsi_dcs_write_seq(ctx->dsi, 0x8B, 0x80);
		dsi_dcs_write_seq(ctx->dsi, 0xC7, 0x0E);
		dsi_dcs_write_seq(ctx->dsi, 0xC8, 0x05);
		dsi_dcs_write_seq(ctx->dsi, 0xC9, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xCA, 0x06);
		dsi_dcs_write_seq(ctx->dsi, 0xCB, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0xCC, 0x03);
		dsi_dcs_write_seq(ctx->dsi, 0xCD, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0xCE, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xCF, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xD0, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xD1, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xD2, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xD3, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xD4, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xD5, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xD6, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xD7, 0x17);
		dsi_dcs_write_seq(ctx->dsi, 0xD8, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xD9, 0x16);
		dsi_dcs_write_seq(ctx->dsi, 0xDA, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xDB, 0x0E);
		dsi_dcs_write_seq(ctx->dsi, 0xDC, 0x01);
		dsi_dcs_write_seq(ctx->dsi, 0xDD, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xDE, 0x02);
		dsi_dcs_write_seq(ctx->dsi, 0xDF, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0xE0, 0x03);
		dsi_dcs_write_seq(ctx->dsi, 0xE1, 0x04);
		dsi_dcs_write_seq(ctx->dsi, 0xE2, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xE3, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xE4, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xE5, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xE6, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xE7, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xE8, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xE9, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xEA, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xEB, 0x17);
		dsi_dcs_write_seq(ctx->dsi, 0xEC, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xED, 0x16);
		dsi_dcs_write_seq(ctx->dsi, 0xEE, 0x1F);
		dsi_dcs_write_seq(ctx->dsi, 0xEF, 0x03);
		dsi_dcs_write_seq(ctx->dsi, 0xFE, 0x70);
		dsi_dcs_write_seq(ctx->dsi, 0x5A, 0x0B);
		dsi_dcs_write_seq(ctx->dsi, 0x5B, 0x0B);
		dsi_dcs_write_seq(ctx->dsi, 0x5C, 0x55);
		dsi_dcs_write_seq(ctx->dsi, 0x5D, 0x24);
		dsi_dcs_write_seq(ctx->dsi, 0xFE, 0x90);
		dsi_dcs_write_seq(ctx->dsi, 0x12, 0x24);
		dsi_dcs_write_seq(ctx->dsi, 0x13, 0x49);
		dsi_dcs_write_seq(ctx->dsi, 0x14, 0x92);
		dsi_dcs_write_seq(ctx->dsi, 0x15, 0x86);
		dsi_dcs_write_seq(ctx->dsi, 0x16, 0x61);
		dsi_dcs_write_seq(ctx->dsi, 0x17, 0x18);
		dsi_dcs_write_seq(ctx->dsi, 0x18, 0x24);
		dsi_dcs_write_seq(ctx->dsi, 0x19, 0x49);
		dsi_dcs_write_seq(ctx->dsi, 0x1A, 0x92);
		dsi_dcs_write_seq(ctx->dsi, 0x1B, 0x86);
		dsi_dcs_write_seq(ctx->dsi, 0x1C, 0x61);
		dsi_dcs_write_seq(ctx->dsi, 0x1D, 0x18);
		dsi_dcs_write_seq(ctx->dsi, 0x1E, 0x24);
		dsi_dcs_write_seq(ctx->dsi, 0x1F, 0x49);
		dsi_dcs_write_seq(ctx->dsi, 0x20, 0x92);
		dsi_dcs_write_seq(ctx->dsi, 0x21, 0x86);
		dsi_dcs_write_seq(ctx->dsi, 0x22, 0x61);
		dsi_dcs_write_seq(ctx->dsi, 0x23, 0x18);
		dsi_dcs_write_seq(ctx->dsi, 0xFE, 0x40);
		dsi_dcs_write_seq(ctx->dsi, 0x0E, 0x10);
		dsi_dcs_write_seq(ctx->dsi, 0xFE, 0xA0);
		dsi_dcs_write_seq(ctx->dsi, 0x04, 0x80);
		dsi_dcs_write_seq(ctx->dsi, 0x16, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x26, 0x10);
		dsi_dcs_write_seq(ctx->dsi, 0x2F, 0x37);
		dsi_dcs_write_seq(ctx->dsi, 0xFE, 0xD0);
		dsi_dcs_write_seq(ctx->dsi, 0x06, 0x0F);
		dsi_dcs_write_seq(ctx->dsi, 0x4B, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x56, 0x4A);
		dsi_dcs_write_seq(ctx->dsi, 0xFE, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0xC2, 0x09);
		dsi_dcs_write_seq(ctx->dsi, 0x35, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0xFE, 0x70);
		dsi_dcs_write_seq(ctx->dsi, 0x7D, 0x61);
		dsi_dcs_write_seq(ctx->dsi, 0x7F, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x7E, 0x4E);
		dsi_dcs_write_seq(ctx->dsi, 0x52, 0x2C);
		dsi_dcs_write_seq(ctx->dsi, 0x49, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x4A, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x4B, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x4C, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x4D, 0xE8);
		dsi_dcs_write_seq(ctx->dsi, 0x4E, 0x25);
		dsi_dcs_write_seq(ctx->dsi, 0x4F, 0x6E);
		dsi_dcs_write_seq(ctx->dsi, 0x50, 0xAE);
		dsi_dcs_write_seq(ctx->dsi, 0x51, 0x2F);
		dsi_dcs_write_seq(ctx->dsi, 0xAD, 0xF4);
		dsi_dcs_write_seq(ctx->dsi, 0xAE, 0x8F);
		dsi_dcs_write_seq(ctx->dsi, 0xAF, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0xB0, 0x54);
		dsi_dcs_write_seq(ctx->dsi, 0xB1, 0x3A);
		dsi_dcs_write_seq(ctx->dsi, 0xB2, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0xB3, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0xB4, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0xB5, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0xB6, 0x18);
		dsi_dcs_write_seq(ctx->dsi, 0xB7, 0x30);
		dsi_dcs_write_seq(ctx->dsi, 0xB8, 0x4A);
		dsi_dcs_write_seq(ctx->dsi, 0xB9, 0x98);
		dsi_dcs_write_seq(ctx->dsi, 0xBA, 0x30);
		dsi_dcs_write_seq(ctx->dsi, 0xBB, 0x60);
		dsi_dcs_write_seq(ctx->dsi, 0xBC, 0x50);
		dsi_dcs_write_seq(ctx->dsi, 0xBD, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0xBE, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0xBF, 0x39);
		dsi_dcs_write_seq(ctx->dsi, 0xFE, 0x00);
		dsi_dcs_write_seq(ctx->dsi, 0x51, 0x66);
	} else {
		ret = mipi_dsi_dcs_write_buffer(ctx->dsi, (u8[]) { 0xfe, 0x00 }, 2);
		if (ret < 0) {
			dev_err(ctx->panel.dev, "cmd set tx 0 failed, ret = %d\n", ret);
			goto power_off;
		}

		ret = mipi_dsi_dcs_write_buffer(ctx->dsi, (u8[]) { 0xc2, 0x08 }, 2);
		if (ret < 0) {
			dev_err(ctx->panel.dev, "cmd set tx 1 failed, ret = %d\n", ret);
			goto power_off;
		}

		ret = mipi_dsi_dcs_write_buffer(ctx->dsi, (u8[]) { 0x35, 0x00 }, 2);
		if (ret < 0) {
			dev_err(ctx->panel.dev, "cmd set tx 2 failed, ret = %d\n", ret);
			goto power_off;
		}

		ret = mipi_dsi_dcs_write_buffer(ctx->dsi, (u8[]) { 0x51, 0xff }, 2);
		if (ret < 0) {
			dev_err(ctx->panel.dev, "cmd set tx 3 failed, ret = %d\n", ret);
			goto power_off;
		}
	}

	ret = mipi_dsi_dcs_write(ctx->dsi, MIPI_DCS_EXIT_SLEEP_MODE, NULL, 0);
	if (ret < 0) {
		dev_err(ctx->panel.dev, "exit_sleep_mode cmd failed ret = %d\n", ret);
		goto power_off;
	}

	/* Per DSI spec wait 120ms after sending exit sleep DCS command */
	msleep(120);

	ret = mipi_dsi_dcs_write(ctx->dsi, MIPI_DCS_SET_DISPLAY_ON, NULL, 0);
	if (ret < 0) {
		dev_err(ctx->panel.dev, "set_display_on cmd failed ret = %d\n", ret);
		goto power_off;
	}

	/* Per DSI spec wait 120ms after sending set_display_on DCS command */
	msleep(120);

	ctx->prepared = true;

	return 0;

power_off:
	return ret;
}

static int visionox_rm69299_get_modes(struct drm_panel *panel,
				      struct drm_connector *connector)
{
	struct visionox_rm69299 *ctx = panel_to_ctx(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, ctx->mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs visionox_rm69299_drm_funcs = {
	.unprepare = visionox_rm69299_unprepare,
	.prepare = visionox_rm69299_prepare,
	.get_modes = visionox_rm69299_get_modes,
};

static int visionox_rm69299_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct visionox_rm69299 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->mode = of_device_get_match_data(dev);

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->panel.dev = dev;
	ctx->dsi = dsi;

	ctx->supplies[0].supply = "vdda";
	ctx->supplies[1].supply = "vdd3p3";

	ret = devm_regulator_bulk_get(ctx->panel.dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(ctx->panel.dev,
					 "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "cannot get reset gpio %ld\n", PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}

	drm_panel_init(&ctx->panel, dev, &visionox_rm69299_drm_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &visionox_rm69299_drm_funcs;
	drm_panel_add(&ctx->panel);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;
			  // | MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO_BURST;
	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "dsi attach failed ret = %d\n", ret);
		goto err_dsi_attach;
	}

	ret = regulator_set_load(ctx->supplies[0].consumer, 32000);
	if (ret) {
		dev_err(dev, "regulator set load failed for vdda supply ret = %d\n", ret);
		goto err_set_load;
	}

	ret = regulator_set_load(ctx->supplies[1].consumer, 13200);
	if (ret) {
		dev_err(dev, "regulator set load failed for vdd3p3 supply ret = %d\n", ret);
		goto err_set_load;
	}

	return 0;

err_set_load:
	mipi_dsi_detach(dsi);
err_dsi_attach:
	drm_panel_remove(&ctx->panel);
	return ret;
}

static int visionox_rm69299_remove(struct mipi_dsi_device *dsi)
{
	struct visionox_rm69299 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(ctx->dsi);
	mipi_dsi_device_unregister(ctx->dsi);

	drm_panel_remove(&ctx->panel);
	return 0;
}

static const struct of_device_id visionox_rm69299_of_match[] = {
	{ .compatible = "visionox,rm69299-1080p-display", 
	  .data = &visionox_rm69299_1080x2248_60hz},
	{ .compatible = "visionox,rm69299-shift", 
	  .data = &visionox_rm69299_1080x2160_60hz},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, visionox_rm69299_of_match);

static struct mipi_dsi_driver visionox_rm69299_driver = {
	.driver = {
		.name = "panel-visionox-rm69299",
		.of_match_table = visionox_rm69299_of_match,
	},
	.probe = visionox_rm69299_probe,
	.remove = visionox_rm69299_remove,
};
module_mipi_dsi_driver(visionox_rm69299_driver);

MODULE_DESCRIPTION("Visionox RM69299 DSI Panel Driver");
MODULE_LICENSE("GPL v2");
