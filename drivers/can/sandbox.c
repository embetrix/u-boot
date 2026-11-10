// SPDX-License-Identifier: GPL-2.0+
#include <can.h>
#include <dm.h>

struct sandbox_can_priv {
	struct can_frame frame;
	bool pending;
	bool loopback;
};

static const struct can_bittiming_limits sandbox_can_limits = {
	.brp_min = 1, .brp_max = 1024,
	.tseg1_min = 2, .tseg1_max = 96,
	.tseg2_min = 2, .tseg2_max = 32,
};

static int sandbox_can_start(struct udevice *dev, const struct can_bittiming *bt,
			     enum can_mode mode)
{
	struct sandbox_can_priv *priv = dev_get_priv(dev);

	priv->pending = false;
	priv->loopback = mode == CAN_MODE_LOOPBACK;
	return 0;
}

static int sandbox_can_stop(struct udevice *dev)
{
	struct sandbox_can_priv *priv = dev_get_priv(dev);

	priv->pending = false;
	return 0;
}

static int sandbox_can_send(struct udevice *dev, const struct can_frame *frame)
{
	struct sandbox_can_priv *priv = dev_get_priv(dev);

	if (priv->pending)
		return -ENOBUFS;
	priv->frame = *frame;
	priv->pending = true;
	return 0;
}

static int sandbox_can_tx_done(struct udevice *dev)
{
	struct sandbox_can_priv *priv = dev_get_priv(dev);

	/* Normal mode models a bus with no other node to acknowledge TX. */
	return priv->loopback ? 0 : -EAGAIN;
}

static int sandbox_can_recv(struct udevice *dev, struct can_frame *frame)
{
	struct sandbox_can_priv *priv = dev_get_priv(dev);

	if (!priv->pending || !priv->loopback)
		return -EAGAIN;
	*frame = priv->frame;
	priv->pending = false;
	return 0;
}

static int sandbox_can_get_status(struct udevice *dev, struct can_status *status)
{
	status->state = CAN_STATE_ERROR_ACTIVE;
	return 0;
}

static int sandbox_can_probe(struct udevice *dev)
{
	struct can_priv *priv = dev_get_uclass_priv(dev);

	priv->clock = 40000000;
	priv->limits = &sandbox_can_limits;
	return 0;
}

static const struct can_ops sandbox_can_ops = {
	.start = sandbox_can_start,
	.stop = sandbox_can_stop,
	.send = sandbox_can_send,
	.tx_done = sandbox_can_tx_done,
	.recv = sandbox_can_recv,
	.get_status = sandbox_can_get_status,
};

static const struct udevice_id sandbox_can_ids[] = {
	{ .compatible = "sandbox,can" },
	{ }
};

U_BOOT_DRIVER(sandbox_can) = {
	.name = "sandbox_can",
	.id = UCLASS_CAN,
	.of_match = sandbox_can_ids,
	.probe = sandbox_can_probe,
	.ops = &sandbox_can_ops,
	.priv_auto = sizeof(struct sandbox_can_priv),
	.flags = DM_FLAG_OS_PREPARE,
};
