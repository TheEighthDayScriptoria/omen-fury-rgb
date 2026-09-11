# omen-fury-linux

Experimental Linux control for HP OEM Kingston FURY DDR5 lighting on selected
HP OMEN desktops.

The project has two deliberately separate parts:

- `omen_fury_wmi.ko`: a minimal out-of-tree kernel bridge exposing only a
  constrained MLED register-write ABI at `/dev/omen-fury-wmi`.
- `omen-furyctl`: a small MIT-licensed userspace controller implementing the
  Windows-derived Off and Static sequences and their timing.

This is an experimental bridge, not a proposed final kernel driver. No effects
engine, timers, audio, profiles, or lighting policy run in kernel space.

> **Safety warning:** this code asks system firmware to access devices on the
> memory SMBus. It has only been derived and partially validated on one OMEN
> platform. A firmware or hardware mismatch can hang a call, disturb lighting,
> or affect system stability. Save work before testing. Start with one DIMM and
> `--dry-run`. Do not use the raw command to explore unknown values casually.

## What is constrained

Userspace cannot choose a WMI GUID, method, HP command, command type, payload
length, or arbitrary payload. The bridge fixes all of these facts:

- GUID `5FB7F034-2C63-45E9-BE91-3D44E2C707E4`
- SGIN `0x55434553`
- COMD `0x00020009`
- CMDT `0x0A`
- WMI method ID `2`
- DSZI `4`, with payload `[reg, value, slave, 0x00]`

It accepts only slaves `C0/C2/C4/C6` and registers
`08/09/20/30/31/32/33`. Calls are serialized and firmware errors are returned
to userspace. The device is root-only by default.

The module does **not** bind to or unbind from I2C, PCI, `i2c_i801`, or
`spd5118`. Those drivers and the DIMM temperature sensors remain untouched.
The observed successful I801 progression `0x01 → 0x41 → 0x42` is documented
evidence only; this project does not access I801 registers directly.

## Build on Fedora 43

The code was prepared and compile-tested with Fedora 43's kernel build system.
For the requested `7.0.11-100.fc43.x86_64` target, boot that kernel and install
its exact development package:

```bash
sudo dnf install gcc make kernel-devel-7.0.11-100.fc43.x86_64
make
make test
```

`make` builds `build/omen-furyctl` and `kernel/omen_fury_wmi.ko`. To build for
a kernel other than the running kernel:

```bash
make module KDIR=/usr/src/kernels/7.0.11-100.fc43.x86_64
```

If that older package is no longer enabled in Fedora repositories, install it
from the matching Fedora Koji build before compiling. Secure Boot systems must
sign the module with an enrolled key; do not disable Secure Boot merely for
convenience.

## Inspect without touching hardware

`--dry-run` does not open the character device or issue any ioctl:

```bash
./build/omen-furyctl --dry-run info
./build/omen-furyctl --dry-run --dimm C0 static FF0000 --brightness 32
./build/omen-furyctl --dry-run off
```

## Install and first hardware test

Installation writes the CLI, example udev rule and systemd unit, and module.
Review the files before running this as root:

```bash
sudo make install
sudo depmod -a
sudo modprobe omen_fury_wmi
ls -l /dev/omen-fury-wmi
sudo /usr/local/bin/omen-furyctl info
```

No module is loaded as part of `make`, `make test`, or `make install`.

Begin with one DIMM and a moderate brightness byte:

```bash
sudo omen-furyctl --verbose --dimm C0 static FF0000 --brightness 32
sudo omen-furyctl --verbose --dimm C0 off
```

Only after verifying one module should you target the default set of all four:

```bash
sudo omen-furyctl static 401020 --brightness 32
sudo omen-furyctl off
```

Brightness is currently the protocol's raw byte, `0..255`, not a claimed
percentage. The conservative default is `32` (`0x20`).

### CLI

```text
omen-furyctl info
omen-furyctl off [--dimm C0|C2|C4|C6]
omen-furyctl static RRGGBB [--brightness 0..255] [--dimm ...]
omen-furyctl --experimental raw SLAVE REG VALUE
```

Options may appear before or after the command. `--verbose` prints each
validated operation. The clearly marked raw command accepts hexadecimal bytes,
requires `--experimental`, checks the reported register whitelist in userspace,
and remains independently constrained by the kernel.

## Optional non-root permissions

The installed udev rule expects a dedicated `omen-fury` group:

```bash
sudo groupadd --system omen-fury
sudo usermod -aG omen-fury "$USER"
sudo udevadm control --reload-rules
sudo udevadm trigger --name-match=omen-fury-wmi
```

Log out and back in for new group membership to apply. Membership grants the
ability to issue every operation allowed by this experimental ABI. Keep the
group small; root-only access is the safer default. To retain root-only access,
do not install the udev rule or remove it from `/usr/lib/udev/rules.d`.

## Restore a chosen state at boot

The example oneshot restores dim static purple at boot. Copy it into `/etc` and
edit `ExecStart` to choose `off` or a color/brightness:

```bash
sudo cp systemd/omen-fury-restore.service /etc/systemd/system/
sudoedit /etc/systemd/system/omen-fury-restore.service
sudo systemctl daemon-reload
sudo systemctl enable omen-fury-restore.service
```

Test your selected command manually before enabling the unit. The service runs
once; it is not a daemon and does not continuously manipulate the bus.

## Uninstall

Disable the service before removing installed files. Do not unload the module
while a CLI operation is in progress.

```bash
sudo systemctl disable --now omen-fury-restore.service
sudo modprobe -r omen_fury_wmi
```

Then remove only the files installed by this project and run `sudo depmod -a`.

## Development and tests

The userspace protocol takes injected write/sleep callbacks. Unit tests assert
the exact per-DIMM static sequence, timing boundaries, and the Windows-derived
reverse-begin/forward-commit Off staging without opening hardware. The CLI test
suite also verifies dry-run behavior and raw-register rejection.

```bash
make test
```

See [architecture](docs/architecture.md), [protocol notes](docs/protocol.md),
and [future work](TODO.md). The transport/protocol split is intended to make a
future OpenRGB controller and eventual mainline kernel interface possible
without preserving this temporary ioctl as the permanent device model.

## License

Userspace, tests, documentation, and build glue are MIT licensed. The kernel
module and UAPI header are GPL-2.0-compatible as identified by their SPDX tags.
