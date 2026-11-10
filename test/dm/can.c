// SPDX-License-Identifier: GPL-2.0+
#include <can.h>
#include <command.h>
#include <dm.h>
#include <generic-phy.h>
#include <asm/gpio.h>
#include <asm/test.h>
#include <dm/test.h>
#include <test/test.h>
#include <test/ut.h>

static int dm_test_can(struct unit_test_state *uts)
{
	struct can_frame tx = { .id = 0x123, .len = 8,
		.data = { 0, 1, 0x7f, 0x80, 0xff, 5, 6, 7 } }, rx;
	struct can_status status;
	struct udevice *dev;
	int len;

	ut_assertok(uclass_get_device_by_name(UCLASS_CAN, "can-test", &dev));
	ut_asserteq(-ENETDOWN, can_recv(dev, &rx));
	ut_asserteq(-ENETDOWN, can_send(dev, &tx, 10));
	ut_assertok(can_start(dev, 1000000, CAN_MODE_LOOPBACK));
	ut_asserteq(-EBUSY, can_start(dev, 500000, CAN_MODE_NORMAL));
	for (len = 0; len <= CAN_MAX_DLEN; len++) {
		tx.len = len;
		ut_assertok(can_send(dev, &tx, 10));
		ut_assertok(can_recv(dev, &rx));
		ut_asserteq(tx.id, rx.id);
		ut_asserteq(tx.len, rx.len);
		ut_assertok(memcmp(tx.data, rx.data, len));
	}
	tx.flags = CAN_FRAME_EFF | CAN_FRAME_RTR;
	tx.id = CAN_EFF_MASK;
	tx.len = 8;
	ut_assertok(can_send(dev, &tx, 10));
	ut_assertok(can_recv(dev, &rx));
	ut_asserteq(tx.flags, rx.flags);
	ut_asserteq(tx.id, rx.id);
	ut_asserteq(-EAGAIN, can_recv(dev, &rx));
	tx.len = 9;
	ut_asserteq(-EINVAL, can_send(dev, &tx, 10));
	tx.len = 8;
	tx.flags = 0;
	ut_asserteq(-EINVAL, can_send(dev, &tx, 10));
	tx.id = 0x123;
	tx.flags = BIT(7);
	ut_asserteq(-EINVAL, can_send(dev, &tx, 10));
	tx.flags = 0;
	ut_assertok(can_stop(dev));
	ut_assertok(can_stop(dev));
	ut_assertok(can_start(dev, 500000, CAN_MODE_LISTEN_ONLY));
	ut_asserteq(-EPERM, can_send(dev, &tx, 10));
	ut_assertok(can_stop(dev));
	ut_assertok(can_start(dev, 500000, CAN_MODE_NORMAL));
	ut_asserteq(-ETIMEDOUT, can_send(dev, &tx, 1));
	ut_assertok(can_get_status(dev, &status));
	ut_asserteq(CAN_STATE_STOPPED, status.state);
	/* Restart after timeout must not deliver the old pending frame. */
	ut_assertok(can_start(dev, 500000, CAN_MODE_LOOPBACK));
	ut_asserteq(-EAGAIN, can_recv(dev, &rx));
	ut_assertok(can_stop(dev));
	return 0;
}

DM_TEST(dm_test_can, UTF_SCAN_FDT);

static int dm_test_can_timing(struct unit_test_state *uts)
{
	struct can_bittiming_limits limits = {
		.brp_min = 1, .brp_max = 1024,
		.tseg1_min = 2, .tseg1_max = 96,
		.tseg2_min = 2, .tseg2_max = 32,
	};
	struct can_bittiming timing;
	u32 rates[] = { 10000, 20000, 50000, 100000, 125000, 250000, 500000, 1000000 };
	int i;

	for (i = 0; i < ARRAY_SIZE(rates); i++) {
		ut_assertok(can_calc_bittiming(40000000, &limits, rates[i], 0, &timing));
		ut_asserteq(40000000, rates[i] * timing.brp *
			    (1 + timing.tseg1 + timing.tseg2));
		ut_assert(timing.tseg1 >= 2 && timing.tseg1 <= 96);
		ut_assert(timing.tseg2 >= 2 && timing.tseg2 <= 32);
	}
	ut_asserteq(-ERANGE, can_calc_bittiming(40000000, &limits, 333333, 0, &timing));
	ut_asserteq(-EINVAL, can_calc_bittiming(40000000, &limits, 0, 0, &timing));
	ut_asserteq(-EINVAL, can_calc_bittiming(40000000, &limits, 2000000, 0, &timing));
	return 0;
}

DM_TEST(dm_test_can_timing, 0);

static int dm_test_can_command(struct unit_test_state *uts)
{
	struct udevice *dev;
	struct can_frame frame;

	if (!CONFIG_IS_ENABLED(CMD_CAN))
		return -EAGAIN;

	ut_assertok(uclass_get_device_by_name(UCLASS_CAN, "can-test", &dev));
	ut_assertok(run_command("can start 0 500000 loopback", 0));
	ut_assertok(run_command("can send 0 eff 1fffffff 0080ff123456789a", 0));
	ut_assertok(can_recv(dev, &frame));
	ut_asserteq(CAN_EFF_MASK, frame.id);
	ut_asserteq(CAN_FRAME_EFF, frame.flags);
	ut_asserteq(8, frame.len);
	ut_asserteq(0x80, frame.data[1]);
	ut_asserteq(0x9a, frame.data[7]);
	ut_assertok(run_command("can send 0 sff 000 -", 0));
	ut_assertok(can_recv(dev, &frame));
	ut_asserteq(0, frame.id);
	ut_asserteq(0, frame.len);
	ut_assertok(run_command("can rtr 0 sff 7ff 8", 0));
	ut_assertok(can_recv(dev, &frame));
	ut_asserteq(CAN_FRAME_RTR, frame.flags);
	ut_asserteq(8, frame.len);
	ut_asserteq(1, run_command("can send 0 sff 800 00", 0));
	ut_assert_nextline("CAN send failed: -22");
	ut_asserteq(1, run_command("can send 0 sff 123 xyz", 0));
	ut_assert_nextline("CAN send failed: -22");
	ut_assertok(run_command("can stop 0", 0));
	ut_assert_console_end();
	return 0;
}

DM_TEST(dm_test_can_command, UTF_SCAN_FDT | UTF_CONSOLE);

static int dm_test_can_phy(struct unit_test_state *uts)
{
	struct udevice *dev, *gpio;
	struct phy first, second;

	if (!CONFIG_IS_ENABLED(PHY_CAN_TRANSCEIVER))
		return -EAGAIN;

	ut_assertok(uclass_get_device_by_name(UCLASS_CAN, "can-test", &dev));
	ut_assertok(uclass_get_device_by_name(UCLASS_GPIO, "base-gpios", &gpio));
	ut_assertok(generic_phy_get_by_index(dev, 0, &first));
	ut_assertok(generic_phy_get_by_index(dev, 0, &second));
	ut_assertok(generic_phy_init(&first));
	ut_assertok(generic_phy_init(&second));
	ut_asserteq(1, sandbox_gpio_get_value(gpio, 6));
	ut_assertok(generic_phy_power_on(&first));
	ut_assertok(generic_phy_power_on(&second));
	ut_asserteq(0, sandbox_gpio_get_value(gpio, 6));
	ut_assertok(generic_phy_power_off(&first));
	ut_asserteq(0, sandbox_gpio_get_value(gpio, 6));
	ut_assertok(generic_phy_power_off(&second));
	ut_asserteq(1, sandbox_gpio_get_value(gpio, 6));
	ut_assertok(generic_phy_exit(&first));
	ut_assertok(generic_phy_exit(&second));
	return 0;
}

DM_TEST(dm_test_can_phy, UTF_SCAN_FDT);
