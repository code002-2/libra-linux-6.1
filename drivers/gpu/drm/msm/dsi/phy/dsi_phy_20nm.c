// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include "dsi_phy.h"
#include "dsi.xml.h"
#include "dsi_phy_20nm.xml.h"

#include <linux/iopoll.h>

#define PLL_SYS_CLK_CTRL		0x000
#define PLL_VCOTAIL_EN			0x004
#define PLL_CMN_MODE			0x008
#define PLL_IE_TRIM			0x00c
#define PLL_IP_TRIM			0x010
#define PLL_CNTRL			0x014
#define PLL_PHSEL_CONTROL		0x018
#define PLL_IPTAT_TRIM_VCCA_TX_SEL	0x01c
#define PLL_IP_SETI			0x024
#define PLL_BKG_KVCO_CAL_EN		0x02c
#define PLL_BIAS_EN_CLKBUFLR_EN		0x030
#define PLL_CP_SETI			0x034
#define PLL_IP_SETP			0x038
#define PLL_CP_SETP			0x03c
#define PLL_SYSCLK_EN_SEL_TXBAND		0x048
#define PLL_RESETSM_CNTRL		0x04c
#define PLL_RESETSM_CNTRL2		0x050
#define PLL_RESETSM_CNTRL3		0x054
#define PLL_DIV_REF1			0x060
#define PLL_DIV_REF2			0x064
#define PLL_KVCO_COUNT1			0x068
#define PLL_KVCO_CAL_CNTRL		0x070
#define PLL_KVCO_CODE			0x074
#define PLL_VREF_CFG3			0x080
#define PLL_PLLLOCK_CMP1		0x090
#define PLL_PLLLOCK_CMP2		0x094
#define PLL_PLLLOCK_CMP3		0x098
#define PLL_PLLLOCK_CMP_EN		0x09c
#define PLL_VCO_TUNE			0x0a8
#define PLL_DEC_START1			0x0ac
#define PLL_SSC_EN_CENTER		0x0b4
#define PLL_FAUX_EN			0x0fc
#define PLL_DIV_FRAC_START1		0x100
#define PLL_DIV_FRAC_START2		0x104
#define PLL_DIV_FRAC_START3		0x108
#define PLL_DEC_START2			0x10c
#define PLL_RXTXEPCLK_EN		0x110
#define PLL_CRCTRL			0x114
#define PLL_LOW_POWER_RO_CONTROL	0x13c
#define PLL_POST_DIVIDER_CONTROL	0x140
#define PLL_HR_OCLK2_DIVIDER		0x144
#define PLL_HR_OCLK3_DIVIDER		0x148
#define PLL_RESET_SM			0x150

#define MSM8992_DSI0_PLL_VCO_RATE	1687680000
#define MSM8992_DSI0_PLL_BYTE_RATE	105480000
#define MSM8992_DSI0_PLL_PIXEL_RATE	140640000

struct dsi_pll_20nm_msm8992 {
	struct clk_hw hw;
	struct msm_dsi_phy *phy;
};

#define to_pll_20nm_msm8992(_hw) \
	container_of(_hw, struct dsi_pll_20nm_msm8992, hw)

static void dsi_pll_20nm_msm8992_powerdown(struct msm_dsi_phy *phy)
{
	void __iomem *base = phy->pll_base;

	dsi_phy_write(base + PLL_SYS_CLK_CTRL, 0x00);
	dsi_phy_write(base + PLL_CMN_MODE, 0x01);
	dsi_phy_write(base + PLL_VCOTAIL_EN, 0x82);
	dsi_phy_write(base + PLL_BIAS_EN_CLKBUFLR_EN, 0x02);
	dsi_phy_write(base + PLL_RESETSM_CNTRL3, 0x06);
	wmb();
	phy->pll_on = false;
}

static int dsi_pll_20nm_msm8992_prepare(struct clk_hw *hw)
{
	struct dsi_pll_20nm_msm8992 *pll = to_pll_20nm_msm8992(hw);
	struct msm_dsi_phy *phy = pll->phy;
	struct device *dev = &phy->pdev->dev;
	void __iomem *base = phy->pll_base;
	u32 status;
	int ret;

	if (phy->pll_on)
		return 0;

	/* Downstream MSM8992 20 nm 90-degree sequence, VCO = 1.68768 GHz. */
	dsi_phy_write(base + PLL_VCOTAIL_EN, 0x82);
	dsi_phy_write(base + PLL_BIAS_EN_CLKBUFLR_EN, 0x2a);
	dsi_phy_write(base + PLL_BIAS_EN_CLKBUFLR_EN, 0x2b);
	dsi_phy_write(base + PLL_RESETSM_CNTRL3, 0x02);

	dsi_phy_write(base + PLL_SYS_CLK_CTRL, 0x40);
	dsi_phy_write(base + PLL_IE_TRIM, 0x0f);
	dsi_phy_write(base + PLL_IP_TRIM, 0x0f);
	dsi_phy_write(base + PLL_PHSEL_CONTROL, 0x08);
	dsi_phy_write(base + PLL_IPTAT_TRIM_VCCA_TX_SEL, 0x0e);
	dsi_phy_write(base + PLL_BKG_KVCO_CAL_EN, 0x08);
	dsi_phy_write(base + PLL_SYSCLK_EN_SEL_TXBAND, 0x4a);
	dsi_phy_write(base + PLL_DIV_REF1, 0x00);
	dsi_phy_write(base + PLL_DIV_REF2, 0x01);
	dsi_phy_write(base + PLL_CNTRL, 0x07);
	dsi_phy_write(base + PLL_KVCO_CAL_CNTRL, 0x1f);
	dsi_phy_write(base + PLL_KVCO_COUNT1, 0x8a);
	dsi_phy_write(base + PLL_VREF_CFG3, 0x10);
	dsi_phy_write(base + PLL_SSC_EN_CENTER, 0x00);
	dsi_phy_write(base + PLL_FAUX_EN, 0x0c);
	dsi_phy_write(base + PLL_RXTXEPCLK_EN, 0x0a);
	dsi_phy_write(base + PLL_LOW_POWER_RO_CONTROL, 0x0f);
	dsi_phy_write(base + PLL_CMN_MODE, 0x00);

	dsi_phy_write(base + PLL_IP_SETI, 0x03);
	dsi_phy_write(base + PLL_CP_SETI, 0x3f);
	dsi_phy_write(base + PLL_IP_SETP, 0x03);
	dsi_phy_write(base + PLL_CP_SETP, 0x1f);
	dsi_phy_write(base + PLL_CRCTRL, 0x77);

	/* 1.68768 GHz / (2 * 19.2 MHz) = 43 + 0xf3333 / 2^20. */
	dsi_phy_write(base + PLL_DIV_FRAC_START1, 0xb3);
	dsi_phy_write(base + PLL_DIV_FRAC_START2, 0xe6);
	dsi_phy_write(base + PLL_DIV_FRAC_START3, 0x7c);
	dsi_phy_write(base + PLL_DEC_START1, 0xab);
	dsi_phy_write(base + PLL_DEC_START2, 0x02);
	dsi_phy_write(base + PLL_PLLLOCK_CMP1, 0x5c);
	dsi_phy_write(base + PLL_PLLLOCK_CMP2, 0x04);
	dsi_phy_write(base + PLL_PLLLOCK_CMP3, 0x00);
	dsi_phy_write(base + PLL_PLLLOCK_CMP_EN, 0x0d);

	/* Indirect path: byte = VCO / 2 / 4 / 2, pixel = VCO / 6 / 2. */
	dsi_phy_write(base + PLL_POST_DIVIDER_CONTROL, 0xa1);
	dsi_phy_write(base + PLL_HR_OCLK2_DIVIDER, 0x03);
	dsi_phy_write(base + PLL_HR_OCLK3_DIVIDER, 0x05);

	dsi_phy_write(base + PLL_KVCO_CODE, 0x00);
	dsi_phy_write(base + PLL_VCO_TUNE, 0x00);
	dsi_phy_write(base + PLL_RESETSM_CNTRL, 0x24);
	dsi_phy_write(base + PLL_RESETSM_CNTRL2, 0x07);
	wmb();
	udelay(1000);

	dsi_phy_write(base + PLL_VCOTAIL_EN, 0x03);
	dsi_phy_write(base + PLL_RESETSM_CNTRL3, 0x02);
	udelay(10);
	dsi_phy_write(base + PLL_RESETSM_CNTRL3, 0x03);
	wmb();

	ret = readl_poll_timeout_atomic(base + PLL_RESET_SM, status,
					(status & GENMASK(6, 5)) == GENMASK(6, 5),
					10, 20000);
	if (ret) {
		DRM_DEV_ERROR(dev, "MSM8992 DSI PLL failed to lock: status=%08x\n",
			      status);
		dsi_pll_20nm_msm8992_powerdown(phy);
		return ret;
	}

	phy->pll_on = true;
	DRM_DEV_INFO(dev, "MSM8992 DSI PLL locked: status=%08x VCO=%u\n",
		     status, MSM8992_DSI0_PLL_VCO_RATE);

	return 0;
}

static void dsi_pll_20nm_msm8992_unprepare(struct clk_hw *hw)
{
	struct dsi_pll_20nm_msm8992 *pll = to_pll_20nm_msm8992(hw);

	if (pll->phy->pll_on)
		dsi_pll_20nm_msm8992_powerdown(pll->phy);
}

static unsigned long dsi_pll_20nm_msm8992_recalc_rate(struct clk_hw *hw,
						       unsigned long parent_rate)
{
	return MSM8992_DSI0_PLL_VCO_RATE;
}

static const struct clk_ops dsi_pll_20nm_msm8992_clk_ops = {
	.prepare = dsi_pll_20nm_msm8992_prepare,
	.unprepare = dsi_pll_20nm_msm8992_unprepare,
	.recalc_rate = dsi_pll_20nm_msm8992_recalc_rate,
};

static int dsi_pll_20nm_msm8992_handoff_init(struct msm_dsi_phy *phy)
{
	struct device *dev = &phy->pdev->dev;
	struct dsi_pll_20nm_msm8992 *pll;
	struct clk_init_data init = { };
	struct clk_hw *byte_hw, *pixel_hw;
	char clk_name[32];
	int ret;

	if (phy->id != DSI_0)
		return -ENODEV;

	pll = devm_kzalloc(dev, sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return -ENOMEM;

	init.name = "dsi0vco_clk";
	init.ops = &dsi_pll_20nm_msm8992_clk_ops;
	init.flags = CLK_IGNORE_UNUSED;
	pll->phy = phy;
	pll->hw.init = &init;
	ret = devm_clk_hw_register(dev, &pll->hw);
	if (ret)
		return ret;
	phy->vco_hw = &pll->hw;

	snprintf(clk_name, sizeof(clk_name), "dsi%dpllbyte", phy->id);
	byte_hw = devm_clk_hw_register_fixed_factor_parent_hw(dev, clk_name,
			&pll->hw, CLK_SET_RATE_PARENT, 1, 16);
	if (IS_ERR(byte_hw))
		return PTR_ERR(byte_hw);

	snprintf(clk_name, sizeof(clk_name), "dsi%dpll", phy->id);
	pixel_hw = devm_clk_hw_register_fixed_factor_parent_hw(dev, clk_name,
			&pll->hw, CLK_SET_RATE_PARENT, 1, 12);
	if (IS_ERR(pixel_hw))
		return PTR_ERR(pixel_hw);

	phy->provided_clocks->hws[DSI_BYTE_PLL_CLK] = byte_hw;
	phy->provided_clocks->hws[DSI_PIXEL_PLL_CLK] = pixel_hw;

	dev_info(dev, "MSM8992 20nm PLL clocks: VCO=%u byte=%u pixel=%u\n",
		 MSM8992_DSI0_PLL_VCO_RATE, MSM8992_DSI0_PLL_BYTE_RATE,
		 MSM8992_DSI0_PLL_PIXEL_RATE);

	return 0;
}

static void dsi_20nm_dphy_set_timing(struct msm_dsi_phy *phy,
		struct msm_dsi_dphy_timing *timing)
{
	void __iomem *base = phy->base;

	dsi_phy_write(base + REG_DSI_20nm_PHY_TIMING_CTRL_0,
		DSI_20nm_PHY_TIMING_CTRL_0_CLK_ZERO(timing->clk_zero));
	dsi_phy_write(base + REG_DSI_20nm_PHY_TIMING_CTRL_1,
		DSI_20nm_PHY_TIMING_CTRL_1_CLK_TRAIL(timing->clk_trail));
	dsi_phy_write(base + REG_DSI_20nm_PHY_TIMING_CTRL_2,
		DSI_20nm_PHY_TIMING_CTRL_2_CLK_PREPARE(timing->clk_prepare));
	if (timing->clk_zero & BIT(8))
		dsi_phy_write(base + REG_DSI_20nm_PHY_TIMING_CTRL_3,
			DSI_20nm_PHY_TIMING_CTRL_3_CLK_ZERO_8);
	dsi_phy_write(base + REG_DSI_20nm_PHY_TIMING_CTRL_4,
		DSI_20nm_PHY_TIMING_CTRL_4_HS_EXIT(timing->hs_exit));
	dsi_phy_write(base + REG_DSI_20nm_PHY_TIMING_CTRL_5,
		DSI_20nm_PHY_TIMING_CTRL_5_HS_ZERO(timing->hs_zero));
	dsi_phy_write(base + REG_DSI_20nm_PHY_TIMING_CTRL_6,
		DSI_20nm_PHY_TIMING_CTRL_6_HS_PREPARE(timing->hs_prepare));
	dsi_phy_write(base + REG_DSI_20nm_PHY_TIMING_CTRL_7,
		DSI_20nm_PHY_TIMING_CTRL_7_HS_TRAIL(timing->hs_trail));
	dsi_phy_write(base + REG_DSI_20nm_PHY_TIMING_CTRL_8,
		DSI_20nm_PHY_TIMING_CTRL_8_HS_RQST(timing->hs_rqst));
	dsi_phy_write(base + REG_DSI_20nm_PHY_TIMING_CTRL_9,
		DSI_20nm_PHY_TIMING_CTRL_9_TA_GO(timing->ta_go) |
		DSI_20nm_PHY_TIMING_CTRL_9_TA_SURE(timing->ta_sure));
	dsi_phy_write(base + REG_DSI_20nm_PHY_TIMING_CTRL_10,
		DSI_20nm_PHY_TIMING_CTRL_10_TA_GET(timing->ta_get));
	dsi_phy_write(base + REG_DSI_20nm_PHY_TIMING_CTRL_11,
		DSI_20nm_PHY_TIMING_CTRL_11_TRIG3_CMD(0));
}

static void dsi_20nm_phy_regulator_ctrl(struct msm_dsi_phy *phy, bool enable)
{
	void __iomem *base = phy->reg_base;

	if (!enable) {
		dsi_phy_write(base + REG_DSI_20nm_PHY_REGULATOR_CAL_PWR_CFG, 0);
		return;
	}

	if (phy->regulator_ldo_mode) {
		dsi_phy_write(phy->base + REG_DSI_20nm_PHY_LDO_CNTRL, 0x1d);
		return;
	}

	/* non LDO mode */
	dsi_phy_write(base + REG_DSI_20nm_PHY_REGULATOR_CTRL_1, 0x03);
	dsi_phy_write(base + REG_DSI_20nm_PHY_REGULATOR_CTRL_2, 0x03);
	dsi_phy_write(base + REG_DSI_20nm_PHY_REGULATOR_CTRL_3, 0x00);
	dsi_phy_write(base + REG_DSI_20nm_PHY_REGULATOR_CTRL_4, 0x20);
	dsi_phy_write(base + REG_DSI_20nm_PHY_REGULATOR_CAL_PWR_CFG, 0x01);
	dsi_phy_write(phy->base + REG_DSI_20nm_PHY_LDO_CNTRL, 0x00);
	dsi_phy_write(base + REG_DSI_20nm_PHY_REGULATOR_CTRL_0, 0x03);
}

static int dsi_20nm_phy_enable(struct msm_dsi_phy *phy,
				struct msm_dsi_phy_clk_request *clk_req)
{
	struct msm_dsi_dphy_timing *timing = &phy->timing;
	int i;
	void __iomem *base = phy->base;
	u32 cfg_4[4] = {0x20, 0x40, 0x20, 0x00};
	u32 val;

	DBG("");

	if (msm_dsi_dphy_timing_calc(timing, clk_req)) {
		DRM_DEV_ERROR(&phy->pdev->dev,
			"%s: D-PHY timing calculation failed\n", __func__);
		return -EINVAL;
	}

	dsi_20nm_phy_regulator_ctrl(phy, true);

	dsi_phy_write(base + REG_DSI_20nm_PHY_STRENGTH_0, 0xff);

	val = dsi_phy_read(base + REG_DSI_20nm_PHY_GLBL_TEST_CTRL);
	if (phy->id == DSI_1 && phy->usecase == MSM_DSI_PHY_STANDALONE)
		val |= DSI_20nm_PHY_GLBL_TEST_CTRL_BITCLK_HS_SEL;
	else
		val &= ~DSI_20nm_PHY_GLBL_TEST_CTRL_BITCLK_HS_SEL;
	dsi_phy_write(base + REG_DSI_20nm_PHY_GLBL_TEST_CTRL, val);

	for (i = 0; i < 4; i++) {
		dsi_phy_write(base + REG_DSI_20nm_PHY_LN_CFG_3(i),
							(i >> 1) * 0x40);
		dsi_phy_write(base + REG_DSI_20nm_PHY_LN_TEST_STR_0(i), 0x01);
		dsi_phy_write(base + REG_DSI_20nm_PHY_LN_TEST_STR_1(i), 0x46);
		dsi_phy_write(base + REG_DSI_20nm_PHY_LN_CFG_0(i), 0x02);
		dsi_phy_write(base + REG_DSI_20nm_PHY_LN_CFG_1(i), 0xa0);
		dsi_phy_write(base + REG_DSI_20nm_PHY_LN_CFG_4(i), cfg_4[i]);
	}

	dsi_phy_write(base + REG_DSI_20nm_PHY_LNCK_CFG_3, 0x80);
	dsi_phy_write(base + REG_DSI_20nm_PHY_LNCK_TEST_STR0, 0x01);
	dsi_phy_write(base + REG_DSI_20nm_PHY_LNCK_TEST_STR1, 0x46);
	dsi_phy_write(base + REG_DSI_20nm_PHY_LNCK_CFG_0, 0x00);
	dsi_phy_write(base + REG_DSI_20nm_PHY_LNCK_CFG_1, 0xa0);
	dsi_phy_write(base + REG_DSI_20nm_PHY_LNCK_CFG_2, 0x00);
	dsi_phy_write(base + REG_DSI_20nm_PHY_LNCK_CFG_4, 0x00);

	dsi_20nm_dphy_set_timing(phy, timing);

	dsi_phy_write(base + REG_DSI_20nm_PHY_CTRL_1, 0x00);

	dsi_phy_write(base + REG_DSI_20nm_PHY_STRENGTH_1, 0x06);

	/* make sure everything is written before enable */
	wmb();
	dsi_phy_write(base + REG_DSI_20nm_PHY_CTRL_0, 0x7f);

	return 0;
}

static void dsi_20nm_phy_disable(struct msm_dsi_phy *phy)
{
	dsi_phy_write(phy->base + REG_DSI_20nm_PHY_CTRL_0, 0);
	dsi_20nm_phy_regulator_ctrl(phy, false);
}

static const struct regulator_bulk_data dsi_phy_20nm_regulators[] = {
	{ .supply = "vddio", .init_load_uA = 100000 },	/* 1.8 V */
	{ .supply = "vcca", .init_load_uA = 10000 },	/* 1.0 V */
};

const struct msm_dsi_phy_cfg dsi_phy_20nm_cfgs = {
	.has_phy_regulator = true,
	.regulator_data = dsi_phy_20nm_regulators,
	.num_regulators = ARRAY_SIZE(dsi_phy_20nm_regulators),
	.ops = {
		.enable = dsi_20nm_phy_enable,
		.disable = dsi_20nm_phy_disable,
	},
	.io_start = { 0xfd998500, 0xfd9a0500 },
	.num_dsi_phy = 2,
};

const struct msm_dsi_phy_cfg dsi_phy_20nm_msm8992_cfgs = {
	.has_phy_regulator = true,
	.regulator_data = dsi_phy_20nm_regulators,
	.num_regulators = ARRAY_SIZE(dsi_phy_20nm_regulators),
	.ops = {
		.enable = dsi_20nm_phy_enable,
		.disable = dsi_20nm_phy_disable,
		.pll_init = dsi_pll_20nm_msm8992_handoff_init,
	},
	.io_start = { 0xfd994500, 0xfd996500 },
	.num_dsi_phy = 2,
};
