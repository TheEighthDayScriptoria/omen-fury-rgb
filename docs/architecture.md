# Architecture

This project deliberately keeps policy out of the kernel.

```text
omen-furyctl
  ├── protocol.c       Off/static ordering and timing
  └── transport.c      narrow ioctl client
          │
          ▼
/dev/omen-fury-wmi
  └── validation + serialization + fixed HP WMI envelope
          │
          ▼
HP firmware MLED method
```

## Kernel boundary

The bridge supports only two ioctls:

- `OMEN_FURY_WMI_GET_CAPS` returns ABI version, flags, allowed DIMM slaves, and
  allowed registers.
- `OMEN_FURY_WMI_MLED_WRITE` accepts exactly `{slave, reg, value, reserved}`.

The kernel fixes the GUID, signature, command, command type, method ID, data
size, and padding. It rejects unknown slaves, unknown registers, and nonzero
reserved fields. A mutex serializes complete WMI evaluations. It exposes no
general WMI method, command, payload, timing, color, effect, or profile API.

The module does not register an I2C adapter or client, access the I801 PCI
function directly, or unbind any driver. `i2c_i801` and all `spd5118` devices
remain bound and untouched. Firmware performs the underlying transaction.

## Userspace boundary

`protocol.c` depends on injected write and sleep callbacks. The CLI uses the
ioctl transport today; a future OpenRGB controller can reuse the protocol
logic behind its own transport interface. If a mainline LED-class interface
replaces the bridge, the command model need not be redesigned.

## Observed transport behavior

The corrected four-byte request was observed to drive Intel I801 status from
`0x01` to `0x41` to `0x42`. This is diagnostic evidence, not an API guarantee;
the bridge never reads or writes I801 registers itself.
