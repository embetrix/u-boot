// SPDX-License-Identifier: GPL-2.0+
#include <can.h>
#include <console.h>
#include <cyclic.h>
#include <dm.h>
#include <time.h>
#include <linux/errno.h>

#define can_get_ops(dev) ((const struct can_ops *)(dev)->driver->ops)

int can_calc_bittiming(u32 clock, const struct can_bittiming_limits *limits,
		       u32 bitrate, u32 sample_point,
		       struct can_bittiming *timing)
{
	struct can_bittiming best = {};
	u32 total, tseg2, brp, sample, error, best_error = UINT_MAX;
	u64 divisor;

	if (!clock || !limits || !timing || !bitrate || bitrate > 1000000 ||
	    sample_point >= 1000)
		return -EINVAL;
	if (!limits->brp_min || limits->brp_min > limits->brp_max ||
	    !limits->tseg1_min || limits->tseg1_min > limits->tseg1_max ||
	    !limits->tseg2_min || limits->tseg2_min > limits->tseg2_max)
		return -EINVAL;
	if (!sample_point)
		sample_point = bitrate > 800000 ? 750 : bitrate > 500000 ? 800 : 875;

	for (total = 1 + limits->tseg1_min + limits->tseg2_min;
	     total <= 1 + limits->tseg1_max + limits->tseg2_max; total++) {
		divisor = (u64)bitrate * total;
		if (divisor > clock || clock % divisor)
			continue;
		brp = clock / divisor;
		if (brp < limits->brp_min || brp > limits->brp_max)
			continue;
		for (tseg2 = limits->tseg2_min; tseg2 <= limits->tseg2_max; tseg2++) {
			if (total <= tseg2 || total - tseg2 - 1 < limits->tseg1_min ||
			    total - tseg2 - 1 > limits->tseg1_max)
				continue;
			sample = 1000 * (total - tseg2) / total;
			error = abs((int)sample - (int)sample_point);
			/* Prefer more quanta when the sample-point error is equal. */
			if (error > best_error)
				continue;
			best_error = error;
			best = (struct can_bittiming) {
				.bitrate = bitrate, .sample_point = sample,
				.brp = brp, .tseg1 = total - tseg2 - 1,
				.tseg2 = tseg2, .sjw = 1,
			};
		}
	}
	if (!best.brp)
		return -ERANGE;
	*timing = best;
	return 0;
}

int can_start(struct udevice *dev, u32 bitrate, enum can_mode mode)
{
	struct can_priv *priv = dev_get_uclass_priv(dev);
	struct can_bittiming timing;
	int ret;

	if (priv->started)
		return -EBUSY;
	if (mode < CAN_MODE_NORMAL || mode > CAN_MODE_LISTEN_ONLY ||
	    (priv->max_bitrate && bitrate > priv->max_bitrate))
		return -EINVAL;
	ret = can_calc_bittiming(priv->clock, priv->limits, bitrate, 0, &timing);
	if (ret)
		return ret;
	ret = can_get_ops(dev)->start(dev, &timing, mode);
	if (ret)
		return ret;
	priv->timing = timing;
	priv->mode = mode;
	priv->started = true;
	return 0;
}

int can_stop(struct udevice *dev)
{
	struct can_priv *priv = dev_get_uclass_priv(dev);
	int ret;

	if (!priv->started)
		return 0;
	can_get_ops(dev)->get_status(dev, &priv->last_status);
	ret = can_get_ops(dev)->stop(dev);
	if (!ret)
		priv->started = false;
	return ret;
}

int can_send(struct udevice *dev, const struct can_frame *frame, uint timeout_ms)
{
	struct can_priv *priv = dev_get_uclass_priv(dev);
	const struct can_ops *ops = can_get_ops(dev);
	ulong start;
	int ret, stop_ret;

	if (!frame || frame->len > CAN_MAX_DLEN ||
	    frame->flags & ~(CAN_FRAME_EFF | CAN_FRAME_RTR) ||
	    frame->id > (frame->flags & CAN_FRAME_EFF ? CAN_EFF_MASK : CAN_SFF_MASK) ||
	    !timeout_ms || timeout_ms > 60000)
		return -EINVAL;
	if (!priv->started)
		return -ENETDOWN;
	if (priv->mode == CAN_MODE_LISTEN_ONLY)
		return -EPERM;
	ret = ops->send(dev, frame);
	if (ret)
		return ret;
	start = get_timer(0);
	for (;;) {
		ret = ops->tx_done(dev);
		if (ret != -EAGAIN)
			break;
		if (ctrlc()) {
			ret = -EINTR;
			break;
		}
		if (get_timer(start) >= timeout_ms) {
			ret = -ETIMEDOUT;
			break;
		}
		schedule();
	}
	if (!ret) {
		priv->tx_frames++;
		return 0;
	}
	/* Do not leave a frame queued for transmission after a failed command. */
	stop_ret = can_stop(dev);
	return stop_ret ? stop_ret : ret;
}

int can_recv(struct udevice *dev, struct can_frame *frame)
{
	struct can_priv *priv = dev_get_uclass_priv(dev);
	int ret;

	if (!frame)
		return -EINVAL;
	if (!priv->started)
		return -ENETDOWN;
	ret = can_get_ops(dev)->recv(dev, frame);
	if (!ret)
		priv->rx_frames++;
	return ret;
}

int can_get_status(struct udevice *dev, struct can_status *status)
{
	struct can_priv *priv = dev_get_uclass_priv(dev);

	if (!status)
		return -EINVAL;
	memset(status, 0, sizeof(*status));
	if (!priv->started) {
		*status = priv->last_status;
		status->state = CAN_STATE_STOPPED;
		return 0;
	}
	return can_get_ops(dev)->get_status(dev, status);
}

static int can_pre_remove(struct udevice *dev)
{
	return can_stop(dev);
}

UCLASS_DRIVER(can) = {
	.id = UCLASS_CAN,
	.name = "can",
	.flags = DM_UC_FLAG_SEQ_ALIAS,
	.per_device_auto = sizeof(struct can_priv),
	.pre_remove = can_pre_remove,
};
