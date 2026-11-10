// SPDX-License-Identifier: GPL-2.0+
#include <can.h>
#include <command.h>
#include <console.h>
#include <cyclic.h>
#include <dm.h>
#include <hexdump.h>
#include <time.h>
#include <dm/uclass.h>
#include <linux/ctype.h>

static int can_number(const char *str, uint base, u32 max, u32 *value)
{
	ulong number;

	if (!*str || *str == '-' || *str == '+' || isspace(*str) ||
	    strict_strtoul(str, base, &number) || number > max)
		return -EINVAL;
	*value = number;
	return 0;
}

static int can_list(void)
{
	struct udevice *dev;
	struct uclass *uc;
	int count = 0;

	/* Listing must not probe controllers or enable transceivers. */
	uclass_id_foreach_dev(UCLASS_CAN, dev, uc) {
		printf("%d: %s (%s)\n", dev_seq(dev), dev->name, dev->driver->name);
		count++;
	}
	if (!count)
		puts("No CAN controllers found\n");
	return 0;
}

static int can_info(struct udevice *dev)
{
	static const char * const states[] = {
		"stopped", "error-active", "error-warning", "error-passive", "bus-off",
	};
	struct can_priv *priv = dev_get_uclass_priv(dev);
	struct can_status status;
	int ret;

	ret = can_get_status(dev, &status);
	if (ret)
		return ret;
	printf("%d: %s clock %u Hz, %s\n", dev_seq(dev), dev->name,
	       priv->clock, states[status.state]);
	if (priv->timing.bitrate)
		printf("bitrate %u sample-point %u.%u%% brp %u tseg1 %u tseg2 %u sjw %u\n",
		       priv->timing.bitrate, priv->timing.sample_point / 10,
		       priv->timing.sample_point % 10, priv->timing.brp,
		       priv->timing.tseg1, priv->timing.tseg2, priv->timing.sjw);
	printf("TX %llu RX %llu, tx-errors %u rx-errors %u rx-overruns %u\n",
	       priv->tx_frames, priv->rx_frames, status.tx_errors,
	       status.rx_errors, status.rx_overruns);
	return 0;
}

static int can_transmit(struct udevice *dev, int argc, char *const argv[], bool rtr)
{
	struct can_frame frame = {};
	u32 timeout = 1000, length;
	int i;

	if (argc < 6 || argc > 7)
		return -EINVAL;
	if (!strcmp(argv[3], "eff"))
		frame.flags = CAN_FRAME_EFF;
	else if (strcmp(argv[3], "sff"))
		return -EINVAL;
	if (can_number(argv[4], 16, frame.flags ? CAN_EFF_MASK : CAN_SFF_MASK,
		       &frame.id))
		return -EINVAL;
	if (argc == 7 && (can_number(argv[6], 10, 60000, &timeout) || !timeout))
		return -EINVAL;
	if (rtr) {
		if (can_number(argv[5], 10, CAN_MAX_DLEN, &length))
			return -EINVAL;
		frame.len = length;
		frame.flags |= CAN_FRAME_RTR;
	} else if (strcmp(argv[5], "-")) {
		length = strlen(argv[5]);
		if (!length || length > 2 * CAN_MAX_DLEN || length % 2)
			return -EINVAL;
		for (i = 0; i < length; i++)
			if (!isxdigit(argv[5][i]))
				return -EINVAL;
		frame.len = length / 2;
		for (i = 0; i < frame.len; i++)
			frame.data[i] = (hex_to_bin(argv[5][2 * i]) << 4) |
				       hex_to_bin(argv[5][2 * i + 1]);
	}
	return can_send(dev, &frame, timeout);
}

static int can_receive(struct udevice *dev, u32 count, u32 timeout)
{
	struct can_frame frame;
	ulong start = get_timer(0);
	u32 received = 0;
	int ret, i;

	while (received < count) {
		if (ctrlc())
			return -EINTR;
		ret = can_recv(dev, &frame);
		if (!ret) {
			printf("%d %s %08x [%u]%s", dev_seq(dev),
			       frame.flags & CAN_FRAME_EFF ? "EFF" : "SFF",
			       frame.id, frame.len, frame.flags & CAN_FRAME_RTR ? " RTR" : "");
			if (!(frame.flags & CAN_FRAME_RTR))
				for (i = 0; i < frame.len; i++)
					printf(" %02x", frame.data[i]);
			putc('\n');
			received++;
		} else if (ret != -EAGAIN) {
			return ret;
		}
		if (received < count && get_timer(start) >= timeout)
			return -ETIMEDOUT;
		schedule();
	}
	return 0;
}

static int do_can(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	struct udevice *dev;
	u32 seq, bitrate, count = 1, timeout = 1000;
	enum can_mode mode = CAN_MODE_NORMAL;
	int ret;

	if (argc == 2 && !strcmp(argv[1], "list"))
		return can_list();
	if (argc < 3 || can_number(argv[2], 10, INT_MAX, &seq))
		return CMD_RET_USAGE;
	ret = uclass_get_device_by_seq(UCLASS_CAN, seq, &dev);
	if (ret)
		goto out;
	if (!strcmp(argv[1], "info") && argc == 3) {
		ret = can_info(dev);
	} else if (!strcmp(argv[1], "stop") && argc == 3) {
		ret = can_stop(dev);
	} else if (!strcmp(argv[1], "start") && (argc == 4 || argc == 5)) {
		if (can_number(argv[3], 10, 1000000, &bitrate) || !bitrate)
			return CMD_RET_USAGE;
		if (argc == 5) {
			if (!strcmp(argv[4], "loopback"))
				mode = CAN_MODE_LOOPBACK;
			else if (!strcmp(argv[4], "listen-only"))
				mode = CAN_MODE_LISTEN_ONLY;
			else
				return CMD_RET_USAGE;
		}
		ret = can_start(dev, bitrate, mode);
	} else if (!strcmp(argv[1], "send") || !strcmp(argv[1], "rtr")) {
		ret = can_transmit(dev, argc, argv, !strcmp(argv[1], "rtr"));
	} else if (!strcmp(argv[1], "recv") && argc <= 5) {
		if ((argc >= 4 && (can_number(argv[3], 10, 1000000, &count) || !count)) ||
		    (argc == 5 && (can_number(argv[4], 10, 60000, &timeout) || !timeout)))
			return CMD_RET_USAGE;
		ret = can_receive(dev, count, timeout);
	} else {
		return CMD_RET_USAGE;
	}
out:
	if (ret)
		printf("CAN %s failed: %d%s\n", argv[1], ret,
		       ret == -ETIMEDOUT ? " (timeout)" : "");
	return ret ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}

U_BOOT_CMD(can, 7, 0, do_can, "send and receive raw Classical CAN frames",
	   "list\n"
	   "can info <dev>\n"
	   "can start <dev> <bitrate> [loopback|listen-only]\n"
	   "can stop <dev>\n"
	   "can send <dev> <sff|eff> <hex-id> <hex-data|-> [timeout-ms]\n"
	   "can rtr <dev> <sff|eff> <hex-id> <length> [timeout-ms]\n"
	   "can recv <dev> [count [timeout-ms]]\n"
	   "    Numbers are decimal except ID/data; '-' sends zero data bytes.\n"
	   "    Default count is 1, timeout 1000 ms. Pending TX failure stops CAN."
);
