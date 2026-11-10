// SPDX-License-Identifier: GPL-2.0+
/* GPIO-controlled TJA1051 CAN transceiver. */
#include <dm.h>
#include <generic-phy.h>
#include <asm/gpio.h>
#include <linux/err.h>

struct can_phy_priv {
	struct gpio_desc *silent;
};

static int can_phy_power_on(struct phy *phy)
{
	struct can_phy_priv *priv = dev_get_priv(phy->dev);

	return priv->silent ? dm_gpio_set_value(priv->silent, 0) : 0;
}

static int can_phy_power_off(struct phy *phy)
{
	struct can_phy_priv *priv = dev_get_priv(phy->dev);

	return priv->silent ? dm_gpio_set_value(priv->silent, 1) : 0;
}

static int can_phy_probe(struct udevice *dev)
{
	struct can_phy_priv *priv = dev_get_priv(dev);
	int flags = GPIOD_IS_OUT | GPIOD_IS_OUT_ACTIVE;

	priv->silent = devm_gpiod_get_optional(dev, "silent", flags);
	return PTR_ERR_OR_ZERO(priv->silent);
}

static const struct phy_ops can_phy_ops = {
	.power_on = can_phy_power_on,
	.power_off = can_phy_power_off,
};

static const struct udevice_id can_phy_ids[] = {
	{ .compatible = "nxp,tja1051" },
	{ }
};

U_BOOT_DRIVER(can_transceiver_phy) = {
	.name = "can_transceiver_phy",
	.id = UCLASS_PHY,
	.of_match = can_phy_ids,
	.probe = can_phy_probe,
	.ops = &can_phy_ops,
	.priv_auto = sizeof(struct can_phy_priv),
};
