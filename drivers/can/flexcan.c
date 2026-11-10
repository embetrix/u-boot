// SPDX-License-Identifier: GPL-2.0-only
/*
 * Polled Classical CAN for i.MX95 FlexCAN.
 * Register layout and initialization follow the Linux FlexCAN driver.
 */
#include <can.h>
#include <clk.h>
#include <dm.h>
#include <generic-phy.h>
#include <asm/io.h>
#include <dm/device_compat.h>
#include <linux/bitfield.h>
#include <linux/iopoll.h>

#define FC_MCR		0x00
#define FC_CTRL1		0x04
#define FC_TIMER		0x08
#define FC_RXMGMASK	0x10
#define FC_RX14MASK	0x14
#define FC_RX15MASK	0x18
#define FC_ECR		0x1c
#define FC_ESR1		0x20
#define FC_IMASK2	0x24
#define FC_IMASK1	0x28
#define FC_IFLAG2	0x2c
#define FC_IFLAG1	0x30
#define FC_CTRL2		0x34
#define FC_RXFGMASK	0x48
#define FC_CBT		0x50
#define FC_MB(n)		(0x80 + (n) * 16)
#define FC_MECR		0xae0
#define FC_FDCTRL	0xc00

#define MCR_MDIS		BIT(31)
#define MCR_FRZ		BIT(30)
#define MCR_HALT		BIT(28)
#define MCR_SOFTRST	BIT(25)
#define MCR_FRZACK	BIT(24)
#define MCR_SUPV		BIT(23)
#define MCR_LPMACK	BIT(20)
#define MCR_SRXDIS	BIT(17)
#define MCR_IRMQ		BIT(16)

#define CTRL1_CLKSRC	BIT(13)
#define CTRL1_LPB	BIT(12)
#define CTRL1_BOFFREC	BIT(6)
#define CTRL1_LBUF	BIT(4)
#define CTRL1_LOM	BIT(3)
#define CTRL2_ECRWRE	BIT(29)
#define CTRL2_WRMFRZ	BIT(28)
#define CTRL2_RRS	BIT(17)
#define CTRL2_EACEN	BIT(16)

#define ESR_FLTCONF	GENMASK(5, 4)
#define ESR_RXWARN	BIT(9)
#define ESR_TXWARN	BIT(8)
#define CBT_BTF		BIT(31)
#define CBT_PRESDIV	GENMASK(30, 21)
#define CBT_RJW		GENMASK(20, 16)
#define CBT_PROPSEG	GENMASK(15, 10)
#define CBT_PSEG1	GENMASK(9, 5)
#define CBT_PSEG2	GENMASK(4, 0)
#define MECR_ECRWRDIS	BIT(31)
#define MECR_HANCEIMSK	BIT(19)
#define MECR_FANCEIMSK	BIT(18)
#define MECR_NCEFAFRZ	BIT(7)

#define MB_CODE		GENMASK(27, 24)
#define MB_RX_BUSY	BIT(24)
#define MB_RX_EMPTY	(4U << 24)
#define MB_RX_FULL	(2U << 24)
#define MB_RX_OVERRUN	(6U << 24)
#define MB_TX_INACTIVE	(8U << 24)
#define MB_TX_DATA	(12U << 24)
#define MB_SRR		BIT(22)
#define MB_IDE		BIT(21)
#define MB_RTR		BIT(20)
#define MB_DLC		GENMASK(19, 16)
#define TX_MB		63
#define FC_TIMEOUT_US	100000

struct flexcan_priv {
	void __iomem *regs;
	struct clk ipg, per;
	struct phy phy;
	u64 rx_pending;
	u32 overruns;
	u8 clk_src;
	bool phy_on;
	bool needs_stop;
};

static const struct can_bittiming_limits flexcan_limits = {
	.brp_min = 1, .brp_max = 1024,
	.tseg1_min = 2, .tseg1_max = 96,
	.tseg2_min = 2, .tseg2_max = 32,
};

static int flexcan_disable(struct flexcan_priv *priv)
{
	u32 reg;

	setbits_le32(priv->regs + FC_MCR, MCR_MDIS);
	return readl_poll_timeout(priv->regs + FC_MCR, reg,
				  reg & MCR_LPMACK, FC_TIMEOUT_US);
}

static int flexcan_stop(struct udevice *dev)
{
	struct flexcan_priv *priv = dev_get_priv(dev);
	u32 reg;
	int ret;

	setbits_le32(priv->regs + FC_MCR, MCR_FRZ | MCR_HALT);
	/* Disable is also attempted if freeze acknowledgment times out. */
	readl_poll_timeout(priv->regs + FC_MCR, reg,
			   reg & MCR_FRZACK, FC_TIMEOUT_US);
	ret = flexcan_disable(priv);
	if (ret)
		return ret;
	if (priv->phy_on) {
		ret = generic_phy_power_off(&priv->phy);
		if (ret)
			return ret;
		priv->phy_on = false;
	}
	priv->needs_stop = false;
	return 0;
}

static int flexcan_start(struct udevice *dev, const struct can_bittiming *bt,
			 enum can_mode mode)
{
	struct flexcan_priv *priv = dev_get_priv(dev);
	void __iomem *regs = priv->regs;
	u32 reg, ctrl, phase1, prop, i;
	int ret, stop_ret;

	/* Retry cleanup if a previous start could not unwind completely. */
	if (priv->needs_stop) {
		ret = flexcan_stop(dev);
		if (ret)
			return ret;
	}
	priv->needs_stop = true;

	/* Select the clock while the module is disabled. */
	ctrl = priv->clk_src ? CTRL1_CLKSRC : 0;
	writel(ctrl, regs + FC_CTRL1);
	clrbits_le32(regs + FC_MCR, MCR_MDIS);
	ret = readl_poll_timeout(regs + FC_MCR, reg,
				 !(reg & MCR_LPMACK), FC_TIMEOUT_US);
	if (ret)
		goto fail;
	setbits_le32(regs + FC_MCR, MCR_SOFTRST);
	ret = readl_poll_timeout(regs + FC_MCR, reg,
				 !(reg & MCR_SOFTRST), FC_TIMEOUT_US);
	if (ret)
		goto fail;
	setbits_le32(regs + FC_MCR, MCR_FRZ | MCR_HALT);
	ret = readl_poll_timeout(regs + FC_MCR, reg,
				 reg & MCR_FRZACK, FC_TIMEOUT_US);
	if (ret)
		goto fail;

	/* Initialize the complete ECC RAM region with aligned word writes. */
	setbits_le32(regs + FC_CTRL2, CTRL2_WRMFRZ);
	for (i = 0x80; i < 0xae0; i += 4)
		writel(0, regs + i);
	clrbits_le32(regs + FC_CTRL2, CTRL2_WRMFRZ);

	phase1 = min(bt->tseg1 / 2, 32U);
	prop = bt->tseg1 - phase1;
	writel(CBT_BTF | FIELD_PREP(CBT_PRESDIV, bt->brp - 1) |
	       FIELD_PREP(CBT_RJW, bt->sjw - 1) |
	       FIELD_PREP(CBT_PROPSEG, prop - 1) |
	       FIELD_PREP(CBT_PSEG1, phase1 - 1) |
	       FIELD_PREP(CBT_PSEG2, bt->tseg2 - 1), regs + FC_CBT);

	ctrl |= CTRL1_BOFFREC | CTRL1_LBUF;
	if (mode == CAN_MODE_LOOPBACK)
		ctrl |= CTRL1_LPB;
	if (mode == CAN_MODE_LISTEN_ONLY)
		ctrl |= CTRL1_LOM;
	writel(ctrl, regs + FC_CTRL1);
	writel(MCR_FRZ | MCR_HALT | MCR_SUPV | MCR_IRMQ | TX_MB |
	       (mode == CAN_MODE_LOOPBACK ? 0 : MCR_SRXDIS), regs + FC_MCR);
	setbits_le32(regs + FC_CTRL2, CTRL2_EACEN | CTRL2_RRS);
	/* Classical CAN: eight-byte mailboxes, CAN FD disabled in MCR. */
	clrbits_le32(regs + FC_FDCTRL, GENMASK(20, 19) | GENMASK(17, 16));
	writel(0, regs + FC_RXMGMASK);
	writel(0, regs + FC_RX14MASK);
	writel(0, regs + FC_RX15MASK);
	writel(0, regs + FC_RXFGMASK);
	/* MB0 is reserved by ERR005829; RX uses MB1..62, TX uses MB63. */
	writel(MB_TX_INACTIVE, regs + FC_MB(0));
	for (i = 1; i < TX_MB; i++)
		writel(MB_RX_EMPTY, regs + FC_MB(i));
	writel(MB_TX_INACTIVE, regs + FC_MB(TX_MB));
	writel(0, regs + FC_IMASK1);
	writel(0, regs + FC_IMASK2);
	writel(~0U, regs + FC_IFLAG1);
	writel(~0U, regs + FC_IFLAG2);
	priv->rx_pending = 0;

	/* Follow the MECR write-unlock sequence; avoid false ECC freezes. */
	setbits_le32(regs + FC_CTRL2, CTRL2_ECRWRE);
	clrbits_le32(regs + FC_MECR, MECR_ECRWRDIS);
	clrbits_le32(regs + FC_MECR,
		     MECR_NCEFAFRZ | MECR_HANCEIMSK | MECR_FANCEIMSK);
	setbits_le32(regs + FC_MECR, MECR_ECRWRDIS);
	clrbits_le32(regs + FC_CTRL2, CTRL2_ECRWRE);

	/* Internal loopback does not need to enable the physical transmitter. */
	if (mode != CAN_MODE_LOOPBACK) {
		ret = generic_phy_power_on(&priv->phy);
		if (ret)
			goto fail;
		priv->phy_on = true;
	}
	clrbits_le32(regs + FC_MCR, MCR_HALT);
	ret = readl_poll_timeout(regs + FC_MCR, reg,
				 !(reg & MCR_FRZACK), FC_TIMEOUT_US);
	if (!ret)
		return 0;
fail:
	stop_ret = flexcan_stop(dev);
	return stop_ret ? stop_ret : ret;
}

static int flexcan_get_status(struct udevice *dev, struct can_status *status)
{
	struct flexcan_priv *priv = dev_get_priv(dev);
	u32 esr = readl(priv->regs + FC_ESR1);
	u32 ecr = readl(priv->regs + FC_ECR);
	u32 fault = FIELD_GET(ESR_FLTCONF, esr);

	status->tx_errors = ecr & 0xff;
	status->rx_errors = (ecr >> 8) & 0xff;
	status->rx_overruns = priv->overruns;
	if (fault >= 2)
		status->state = CAN_STATE_BUS_OFF;
	else if (fault == 1)
		status->state = CAN_STATE_ERROR_PASSIVE;
	else if (esr & (ESR_RXWARN | ESR_TXWARN))
		status->state = CAN_STATE_ERROR_WARNING;
	else
		status->state = CAN_STATE_ERROR_ACTIVE;
	return 0;
}

static bool flexcan_bus_off(struct flexcan_priv *priv)
{
	return FIELD_GET(ESR_FLTCONF, readl(priv->regs + FC_ESR1)) >= 2;
}

static int flexcan_send(struct udevice *dev, const struct can_frame *frame)
{
	struct flexcan_priv *priv = dev_get_priv(dev);
	void __iomem *mb = priv->regs + FC_MB(TX_MB);
	u32 ctrl = MB_TX_DATA | FIELD_PREP(MB_DLC, frame->len);
	u32 id = frame->id << 18, data;
	int i, j;

	if (flexcan_bus_off(priv))
		return -ENETDOWN;
	if (frame->flags & CAN_FRAME_EFF) {
		ctrl |= MB_IDE | MB_SRR;
		id = frame->id;
	}
	if (frame->flags & CAN_FRAME_RTR)
		ctrl |= MB_RTR;
	writel(MB_TX_INACTIVE, mb);
	writel(BIT(31), priv->regs + FC_IFLAG2);
	for (i = 0; i < CAN_MAX_DLEN; i += 4) {
		data = 0;
		for (j = 0; j < 4; j++) {
			data <<= 8;
			if (!(frame->flags & CAN_FRAME_RTR) && i + j < frame->len)
				data |= frame->data[i + j];
		}
		writel(data, mb + 8 + i);
	}
	writel(id, mb + 4);
	writel(ctrl, mb);
	/* ERR005829: write the reserved mailbox inactive twice after TX. */
	writel(MB_TX_INACTIVE, priv->regs + FC_MB(0));
	writel(MB_TX_INACTIVE, priv->regs + FC_MB(0));
	return 0;
}

static int flexcan_tx_done(struct udevice *dev)
{
	struct flexcan_priv *priv = dev_get_priv(dev);

	if (readl(priv->regs + FC_IFLAG2) & BIT(31)) {
		writel(BIT(31), priv->regs + FC_IFLAG2);
		/* RTR transmissions can leave a mailbox configured for reception. */
		writel(MB_TX_INACTIVE, priv->regs + FC_MB(TX_MB));
		return 0;
	}
	return flexcan_bus_off(priv) ? -ENETDOWN : -EAGAIN;
}

static int flexcan_recv(struct udevice *dev, struct can_frame *frame)
{
	struct flexcan_priv *priv = dev_get_priv(dev);
	void __iomem *regs = priv->regs, *mb, *iflag;
	u32 ctrl, id, data;
	int n, i, j;

	/*
	 * Drain a snapshot before accepting refilled low-numbered mailboxes.
	 * Otherwise a newly received MB1 could overtake an older MB2 frame.
	 */
	if (!priv->rx_pending) {
		priv->rx_pending = readl(regs + FC_IFLAG1) & ~BIT(0);
		priv->rx_pending |= (u64)(readl(regs + FC_IFLAG2) & ~BIT(31)) << 32;
	}
	if (!priv->rx_pending)
		return flexcan_bus_off(priv) ? -ENETDOWN : -EAGAIN;
	n = __ffs64(priv->rx_pending);
	iflag = regs + (n < 32 ? FC_IFLAG1 : FC_IFLAG2);
	mb = regs + FC_MB(n);
	ctrl = readl(mb);
	if (ctrl & MB_RX_BUSY)
		return -EAGAIN;
	if ((ctrl & MB_CODE) != MB_RX_FULL && (ctrl & MB_CODE) != MB_RX_OVERRUN) {
		readl(regs + FC_TIMER);
		return -EAGAIN;
	}
	if ((ctrl & MB_CODE) == MB_RX_OVERRUN)
		priv->overruns++;
	memset(frame, 0, sizeof(*frame));
	id = readl(mb + 4);
	frame->id = ctrl & MB_IDE ? id & CAN_EFF_MASK : (id >> 18) & CAN_SFF_MASK;
	frame->flags = (ctrl & MB_IDE ? CAN_FRAME_EFF : 0) |
		       (ctrl & MB_RTR ? CAN_FRAME_RTR : 0);
	frame->len = min_t(u32, FIELD_GET(MB_DLC, ctrl), CAN_MAX_DLEN);
	if (!(frame->flags & CAN_FRAME_RTR)) {
		for (i = 0; i < frame->len; i += 4) {
			data = readl(mb + 8 + i);
			for (j = 0; j < 4 && i + j < frame->len; j++)
				frame->data[i + j] = data >> (24 - j * 8);
		}
	}
	writel(BIT(n % 32), iflag);
	priv->rx_pending &= ~BIT_ULL(n);
	/* TIMER read unlocks the mailbox and permits automatic rearming. */
	readl(regs + FC_TIMER);
	return 0;
}

static int flexcan_probe(struct udevice *dev)
{
	struct flexcan_priv *priv = dev_get_priv(dev);
	struct can_priv *can = dev_get_uclass_priv(dev);
	ulong rate;
	int ret;

	priv->regs = dev_read_addr_ptr(dev);
	if (!priv->regs)
		return -EINVAL;
	/* The initial implementation supports the i.MX95 little-endian IP. */
	if (dev_read_bool(dev, "big-endian") ||
	    dev_read_prop(dev, "xceiver-supply", NULL))
		return -EOPNOTSUPP;
	priv->clk_src = dev_read_u8_default(dev, "fsl,clk-source", 1);
	if (priv->clk_src > 1)
		return -EINVAL;
	ret = clk_get_by_name(dev, "ipg", &priv->ipg);
	if (ret)
		return ret;
	ret = clk_get_by_name(dev, "per", &priv->per);
	if (ret)
		return ret;
	ret = clk_enable(&priv->ipg);
	if (ret)
		return ret;
	ret = clk_enable(&priv->per);
	if (ret)
		goto disable_ipg;
	rate = clk_get_rate(&priv->per);
	if (IS_ERR_VALUE(rate) || !rate || rate > UINT_MAX) {
		ret = -EINVAL;
		goto disable_per;
	}
	can->clock = rate;
	can->limits = &flexcan_limits;
	ret = flexcan_disable(priv);
	if (ret)
		goto disable_per;
	if (dev_read_prop(dev, "phys", NULL)) {
		ret = generic_phy_get_by_index(dev, 0, &priv->phy);
		if (ret)
			goto disable_per;
		can->max_bitrate = dev_read_u32_default(priv->phy.dev, "max-bitrate", 0);
		ret = generic_phy_init(&priv->phy);
		if (ret)
			goto disable_per;
	}
	return 0;
disable_per:
	clk_disable(&priv->per);
disable_ipg:
	clk_disable(&priv->ipg);
	return ret;
}

static int flexcan_remove(struct udevice *dev)
{
	struct flexcan_priv *priv = dev_get_priv(dev);
	int ret;

	/* The uclass also removes devices whose start never succeeded. */
	if (priv->needs_stop) {
		ret = flexcan_stop(dev);
		if (ret)
			return ret;
	}

	generic_phy_exit(&priv->phy);
	clk_disable(&priv->per);
	clk_disable(&priv->ipg);
	return 0;
}

static const struct can_ops flexcan_ops = {
	.start = flexcan_start,
	.stop = flexcan_stop,
	.send = flexcan_send,
	.tx_done = flexcan_tx_done,
	.recv = flexcan_recv,
	.get_status = flexcan_get_status,
};

static const struct udevice_id flexcan_ids[] = {
	{ .compatible = "fsl,imx95-flexcan" },
	{ }
};

U_BOOT_DRIVER(flexcan) = {
	.name = "flexcan",
	.id = UCLASS_CAN,
	.of_match = flexcan_ids,
	.probe = flexcan_probe,
	.remove = flexcan_remove,
	.ops = &flexcan_ops,
	.priv_auto = sizeof(struct flexcan_priv),
	.flags = DM_FLAG_OS_PREPARE,
};
