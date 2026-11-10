.. SPDX-License-Identifier: GPL-2.0+

CAN drivers
===========

``UCLASS_CAN`` provides raw Classical CAN operations independently of U-Boot's
Ethernet networking stack. See :doc:`../../usage/cmd/can` for the command.
The API is declared in ``include/can.h``; it does not implement the SocketCAN
userspace ABI. Identifier flags are separate from the identifier value.

A controller populates ``struct can_priv`` with its protocol clock, timing
limits and optional maximum bitrate during probe. The common layer computes
exact-rate timing and validates frame lengths, identifier ranges and modes.
It owns started/stopped state and successful TX/RX counters. Drivers implement
``start``, ``stop``, ``send``, ``tx_done``, ``recv`` and ``get_status``.

``send`` submits one frame and ``tx_done`` returns zero only on completed
transmission, ``-EAGAIN`` while pending, or a negative error. ``can_send``
performs a bounded poll, services cyclic work and supports Ctrl-C. On completion
failure it calls ``stop`` before returning, so drivers must quiesce pending
transmission. ``recv`` is nonblocking and returns ``-EAGAIN`` for an empty
controller. Drivers return Classical RTR length without interpreting the
unused data bytes. Protocol errors are exposed through controller state and
counters, not synthetic received frames. Bus-off recovery requires stop/start.

Probe must not join the CAN bus. Start failures must unwind resources acquired
by start; stop must be idempotent through the public API. Register drivers with
``DM_FLAG_OS_PREPARE`` to stop active controllers before OS handoff. The CAN
uclass pre-remove callback stops the device; the driver's remove callback then
releases clocks and PHY initialization. No SPL CAN support is provided.
The FlexCAN driver retains a cleanup flag if unwinding a failed start fails,
and retries that cleanup before another start or removal. A cleanup error is
propagated instead of silently releasing clocks or acquiring another PHY user.

The i.MX95 FlexCAN driver uses extended nominal timing, Classical eight-byte
mailboxes, ECC RAM initialization, and the controller's mailbox mode. MB0 is
reserved for ERR005829, MB63 is used for transmit, and MB1..62 receive frames.
Pending RX mailboxes are drained as a snapshot before polling for new flags,
preventing refilled low-numbered mailboxes from overtaking the current batch.
Interrupts are masked; only status flags are polled. Mode changes use bounded
freeze/disable waits. Transceiver controls belong to a PHY driver. Multiple
controllers referencing the same PHY node use generic PHY reference counting,
so stopping one does not silence another active user.

Tests
-----

The sandbox driver models internal loopback and an unacknowledged external
transmission. Build ``sandbox_defconfig`` and run::

    ./u-boot -T -c 'ut dm can'
    ./u-boot -T -c 'ut dm can_timing'
    ./u-boot -T -c 'ut dm can_command'
    ./u-boot -T -c 'ut dm can_phy'

Hardware validation must additionally exercise standard/extended data and RTR
frames in both directions, empty and full payloads, exact bitrates, internal
loopback, listen-only mode, timeout cleanup, restart, and shared PHY lifetime.
Mailbox overruns and console throughput impose limits on sustained reception.
Listen-only reception of valid traffic requires two other active bus nodes;
a two-node setup can verify transmit rejection and the absence of ACKs only.
