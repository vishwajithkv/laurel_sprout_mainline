// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2025 Kamil Gołda <kamil.golda@protonmail.com>
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
// Copyright (c) 2025, The Linux Foundation. All rights reserved.

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct s6e8fco_samsungp {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data supplies[3];
	struct gpio_desc *reset_gpio;
};

static inline
struct s6e8fco_samsungp *to_s6e8fco_samsungp(struct drm_panel *panel)
{
	return container_of(panel, struct s6e8fco_samsungp, panel);
}

static void s6e8fco_samsungp_reset(struct s6e8fco_samsungp *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(12000, 13000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int s6e8fco_samsungp_on(struct s6e8fco_samsungp *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfc, 0x5a, 0x5a);

	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x0000);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	msleep(50);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x04, 0xed);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xed, 0xe4, 0x08, 0x96, 0xa4,
						0x2a, 0x72, 0xe2, 0xca, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfc, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfc, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe1, 0x93);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x05, 0xf4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf4, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xed, 0x01, 0x81, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfc, 0xa5, 0xa5);

	return dsi_ctx.accum_err;
}

static int s6e8fco_samsungp_off(struct s6e8fco_samsungp *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	msleep(20);

	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	msleep(120);

	return dsi_ctx.accum_err;
}

static int s6e8fco_samsungp_prepare(struct drm_panel *panel)
{
	struct s6e8fco_samsungp *ctx = to_s6e8fco_samsungp(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	s6e8fco_samsungp_reset(ctx);

	ret = s6e8fco_samsungp_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int s6e8fco_samsungp_unprepare(struct drm_panel *panel)
{
	struct s6e8fco_samsungp *ctx = to_s6e8fco_samsungp(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = s6e8fco_samsungp_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode s6e8fco_samsungp_mode = {
	.clock = (720 + 350 + 40 + 294) * (1560 + 17 + 2 + 5) * 60 / 1000,
	.hdisplay = 720,
	.hsync_start = 720 + 350,
	.hsync_end = 720 + 350 + 40,
	.htotal = 720 + 350 + 40 + 294,
	.vdisplay = 1560,
	.vsync_start = 1560 + 17,
	.vsync_end = 1560 + 17 + 2,
	.vtotal = 1560 + 17 + 2 + 5,
	.width_mm = 65,
	.height_mm = 140,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int s6e8fco_samsungp_get_modes(struct drm_panel *panel,
				      struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &s6e8fco_samsungp_mode);
}

static const struct drm_panel_funcs s6e8fco_samsungp_panel_funcs = {
	.prepare = s6e8fco_samsungp_prepare,
	.unprepare = s6e8fco_samsungp_unprepare,
	.get_modes = s6e8fco_samsungp_get_modes,
};

static int s6e8fco_samsungp_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static int s6e8fco_samsungp_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness_large(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness;
}

static const struct backlight_ops s6e8fco_samsungp_bl_ops = {
	.update_status = s6e8fco_samsungp_bl_update_status,
	.get_brightness = s6e8fco_samsungp_bl_get_brightness,
};

static struct backlight_device *
s6e8fco_samsungp_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 268,
		.max_brightness = 2047,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &s6e8fco_samsungp_bl_ops, &props);
}

static int s6e8fco_samsungp_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct s6e8fco_samsungp *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supplies[0].supply = "vddio";
	ctx->supplies[1].supply = "ldo";
	ctx->supplies[2].supply = "iovcc";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &s6e8fco_samsungp_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = s6e8fco_samsungp_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void s6e8fco_samsungp_remove(struct mipi_dsi_device *dsi)
{
	struct s6e8fco_samsungp *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id s6e8fco_samsungp_of_match[] = {
	{ .compatible = "samsung,s6e8fco" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s6e8fco_samsungp_of_match);

static struct mipi_dsi_driver s6e8fco_samsungp_driver = {
	.probe = s6e8fco_samsungp_probe,
	.remove = s6e8fco_samsungp_remove,
	.driver = {
		.name = "panel-samsung-s6e8fco",
		.of_match_table = s6e8fco_samsungp_of_match,
	},
};
module_mipi_dsi_driver(s6e8fco_samsungp_driver);

MODULE_AUTHOR("Kamil Gołda <kamil.golda@protonmail.com>");
MODULE_DESCRIPTION("DRM driver for s6e8fco samsung amoled video mode dsi panel");
MODULE_LICENSE("GPL");
