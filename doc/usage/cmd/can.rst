.. SPDX-License-Identifier: GPL-2.0+

can command
===========

Synopsis
--------

::

    can list
    can info <dev>
    can start <dev> <bitrate> [loopback|listen-only]
    can stop <dev>
    can send <dev> <sff|eff> <hex-id> <hex-data|-> [timeout-ms]
    can rtr <dev> <sff|eff> <hex-id> <length> [timeout-ms]
    can recv <dev> [count [timeout-ms]]

Description
-----------

The command exchanges raw Classical CAN frames. It supports 11-bit (``sff``)
and 29-bit (``eff``) identifiers, zero to eight payload bytes, and remote
transmission requests (RTR). CAN FD and higher-level protocols are not provided.

``list`` lists bound controllers without probing or starting them. ``dev`` is
the driver-model sequence number, optionally assigned by a ``canN`` DT alias;
it is not necessarily the number printed on a board connector. ``info`` probes
the selected controller and reports its clock, last configured timing, state,
successful frame counts, controller error counters and observed overruns.
While stopped, error counters are the snapshot taken before the last stop.

``start`` requires an explicit bitrate in bits per second. The bus peer must
use the same bitrate. Only rates exactly representable by the controller clock
and timing limits are accepted, up to 1000000 bit/s. Sample points are selected
automatically: 75% above 800 kbit/s, 80% above 500 kbit/s, and 87.5% otherwise;
the nearest representable sample point is reported by ``info``. Stop the
controller before changing its bitrate or mode.

``loopback`` enables internal controller loopback. ``listen-only`` receives
without acknowledging other nodes and disallows transmission. Normal mode
acknowledges valid received frames even while U-Boot is at the command prompt.
An external normal-mode peer is needed to acknowledge normal transmissions.
To receive acknowledged traffic in listen-only mode, a bus needs both a sender
and another active receiver. On a two-node bus, making the only receiver silent
leaves the sender without acknowledgments; FlexCAN rejects those frames.

Identifiers and payloads are hexadecimal. All other numbers are decimal.
Payload bytes are supplied as one string; ``-`` means zero payload bytes.
``rtr`` takes a requested payload length, not payload bytes.

Transmit waits for completion, not merely placement into a mailbox. The
default timeout is 1000 ms; valid explicit timeouts are 1..60000 ms. A pending
transmission that times out, is cancelled with Ctrl-C, or encounters a driver
completion error causes the controller to be stopped. Start it again before
retrying. CAN acknowledgment confirms reception by a bus node, not processing
by a particular application.

``recv`` defaults to one frame and a 1000 ms timeout. Count may be 1..1000000.
The timeout covers the entire receive command, including printing all frames.
Ctrl-C cancels reception. Receive timeout/cancellation leaves the controller
running, and frames already printed remain valid. Mailboxes provide finite
buffering while other commands run; there is no continuous background drain.
This command is for diagnostics and bounded exchanges, not lossless capture
of a saturated bus. Stopping/restarting discards buffered frames.

Example
-------

::

    => can list
    0: can@425b0000 (flexcan)
    1: can@427d0000 (flexcan)
    => can start 0 1000000 loopback
    => can send 0 sff 123 0080ff42
    => can recv 0 1 1000
    0 SFF 00000123 [4] 00 80 ff 42
    => can stop 0
    => can start 0 1000000
    => can send 0 eff 18daf110 0102030405060708
    => can rtr 0 sff 321 8
    => can recv 0 1 1000
    => can stop 0

The external examples require a connected, correctly terminated bus peer.

Configuration
-------------

Enable ``CONFIG_CAN`` and ``CONFIG_CMD_CAN``, and a CAN controller driver.
For i.MX95 use ``CONFIG_CAN_FLEXCAN``. GPIO-controlled TJA1051 transceivers
need ``CONFIG_PHY_CAN_TRANSCEIVER``. Enable ``CONFIG_GPIO_HOG`` when board
routing depends on DT GPIO hogs, as on FRDM-IMX95.

Use the controller and PHY nodes from the U-Boot control DT, including
``status``, clocks, pinctrl and ``phys``. A transceiver's ``max-bitrate`` is
a capability limit, not the operating bitrate. Shared physical control must
be described by a shared PHY node so its users are reference counted.

On the tested FRDM-IMX95, the CAN0 connector reaches FlexCAN2 at ``0x425b0000``
(sequence 0 without aliases); FlexCAN5 at ``0x427d0000`` is sequence 1. The
board's CAN/GPIO routing hog must be enabled. Both controllers reference the
same TJA1051 PHY node and therefore share its silent control. The initial
FlexCAN driver supports ``phys`` or an always-enabled external transceiver;
``xceiver-supply`` regulator control and big-endian registers are not supported
and cause probe to fail explicitly.

Return value
------------

``$?`` is zero on success and nonzero on invalid arguments, probe/configuration
failure, transmit failure, bus-off, receive timeout or cancellation. A receive
command which prints fewer frames than requested before timing out also fails.
