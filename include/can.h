/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __CAN_H
#define __CAN_H

#include <linux/types.h>
#include <linux/bitops.h>

struct udevice;

#define CAN_MAX_DLEN	8
#define CAN_SFF_MASK	0x7ffU
#define CAN_EFF_MASK	0x1fffffffU
#define CAN_FRAME_EFF	BIT(0)
#define CAN_FRAME_RTR	BIT(1)

/**
 * struct can_frame - Classical CAN frame (not a SocketCAN ABI)
 * @id: Identifier, without flags
 * @len: Payload length, or requested length for RTR (0..8)
 * @flags: CAN_FRAME_EFF and/or CAN_FRAME_RTR
 * @data: Payload bytes; ignored for RTR
 */
struct can_frame {
	u32 id;
	u8 len;
	u8 flags;
	u8 data[CAN_MAX_DLEN];
};

enum can_mode {
	CAN_MODE_NORMAL,
	CAN_MODE_LOOPBACK,
	CAN_MODE_LISTEN_ONLY,
};

enum can_state {
	CAN_STATE_STOPPED,
	CAN_STATE_ERROR_ACTIVE,
	CAN_STATE_ERROR_WARNING,
	CAN_STATE_ERROR_PASSIVE,
	CAN_STATE_BUS_OFF,
};

/**
 * struct can_bittiming - Nominal bit timing
 * @bitrate: Bits per second
 * @sample_point: Sample point in parts per thousand
 * @brp: Clock prescaler
 * @tseg1: Propagation plus phase segment 1, in time quanta
 * @tseg2: Phase segment 2, in time quanta
 * @sjw: Synchronization jump width, in time quanta
 */
struct can_bittiming {
	u32 bitrate;
	u32 sample_point;
	u32 brp;
	u32 tseg1;
	u32 tseg2;
	u32 sjw;
};

struct can_bittiming_limits {
	u32 brp_min, brp_max;
	u32 tseg1_min, tseg1_max;
	u32 tseg2_min, tseg2_max;
};

struct can_status {
	enum can_state state;
	u32 tx_errors;
	u32 rx_errors;
	u32 rx_overruns;
};

/**
 * struct can_priv - Per-device uclass data
 * @clock: Protocol clock frequency in Hz, supplied by the driver's probe
 * @max_bitrate: Optional transceiver limit (zero means unspecified)
 * @limits: Driver-supplied bit-timing limits (unit prescaler increments)
 * @timing: Last successfully configured timing
 * @mode: Last successfully configured mode
 * @started: Whether the controller has been started
 * @tx_frames: Successfully completed transmissions since probe
 * @rx_frames: Frames returned to callers since probe
 * @last_status: Error counters captured before the last stop
 */
struct can_priv {
	u32 clock;
	u32 max_bitrate;
	const struct can_bittiming_limits *limits;
	struct can_bittiming timing;
	enum can_mode mode;
	bool started;
	u64 tx_frames;
	u64 rx_frames;
	struct can_status last_status;
};

/**
 * struct can_ops - Controller operations, called on probed devices
 * @start: Configure and start; unwind resources on failure
 * @stop: Quiesce transmission and reception, including pending TX
 * @send: Submit one frame, without waiting for completion
 * @tx_done: Return 0 on completion, -EAGAIN while pending, or an error
 * @recv: Return one frame, -EAGAIN if empty, or an error
 * @get_status: Read controller state and error counters
 */
struct can_ops {
	int (*start)(struct udevice *dev, const struct can_bittiming *timing,
		     enum can_mode mode);
	int (*stop)(struct udevice *dev);
	int (*send)(struct udevice *dev, const struct can_frame *frame);
	int (*tx_done)(struct udevice *dev);
	int (*recv)(struct udevice *dev, struct can_frame *frame);
	int (*get_status)(struct udevice *dev, struct can_status *status);
};

/* Only exact bitrates are accepted; sample_point == 0 selects a default. */
int can_calc_bittiming(u32 clock, const struct can_bittiming_limits *limits,
		       u32 bitrate, u32 sample_point,
		       struct can_bittiming *timing);
int can_start(struct udevice *dev, u32 bitrate, enum can_mode mode);
int can_stop(struct udevice *dev);
/* Bounded synchronous TX; timeout or cancellation stops the controller. */
int can_send(struct udevice *dev, const struct can_frame *frame, uint timeout_ms);
/* Nonblocking receive. Returns -EAGAIN if no frame is available. */
int can_recv(struct udevice *dev, struct can_frame *frame);
int can_get_status(struct udevice *dev, struct can_status *status);

#endif
