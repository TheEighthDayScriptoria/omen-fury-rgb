# HP OMEN / Kingston FURY DDR5 protocol notes

This document records the protocol knowledge behind this project. It separates
what the Linux implementation currently permits from behavior recovered by
static analysis of HP's Windows software. Static analysis establishes the
requests HP's software constructs; it does **not** prove that an unimplemented
request has been exercised successfully on the target Linux system.

## Evidence levels

| Label | Meaning |
|---|---|
| Implemented | Available through the constrained Linux bridge and CLI, with unit tests for ordering and timing |
| Windows-derived | Recovered from HP's Windows software by static analysis, but not yet exposed by this project or tested here on hardware |
| Observed | Seen during earlier hardware tracing; recorded as evidence rather than treated as a stable API guarantee |

Off and Static are **Implemented**. Wave, Breathing, Blinking, and Color Cycle
are **Windows-derived**. No dynamic effect described below was executed during
this analysis, and no Windows binary was run.

## Source provenance

The dynamic-effect findings were recovered on 2026-09-11 from the official
[Microsoft Store OMEN Gaming Hub package](https://apps.microsoft.com/detail/9nqdw009t0t5):

| Artifact | Value |
|---|---|
| Product | OMEN Gaming Hub, Microsoft Store product `9NQDW009T0T5` |
| Package version | `1101.2608.3.0` (x64) |
| Package SHA-256 | `27b078a65b7476e59c276d87be9c3f927d9a181d9bff2b13fb7398c3401adf68` |
| `HP.Omen.Background.MemoryLightingBg.dll` SHA-256 | `f7b19bc567a79e7c19e7e0a989b7c3bc520225a4c4cc2739714ba136cc5fc588` |
| `HP.Omen.Core.Common.dll` SHA-256 | `6957af182911a3a6bce246f827715309fb109c8edee7a2a22b23522c40b3acdb` |

The package and DLLs are not redistributed by this repository. The analysis
also inspected `HP.Omen.Core.Model.DataStructure.dll`,
`HP.Omen.MemoryLightingModule.dll`, and its English resource assembly. The
FURY DDR5 UI constructs exactly four effect choices: Wave, Breathing,
Blinking, and Color Cycle. Shared enums contain names such as Flame, Raindrop,
and Starlight, but the FURY DDR5 controller path neither implements nor exposes
them; they must not be treated as supported modes for this device.

The recovered Windows transport path is:

```text
MemoryLightingBg
  -> OmenHsaClient.ExecuteBiosWmiCommand(0x00020009, 0x0A, payload, 4)
  -> Global\Access_PCI mutex
  -> \\.\HpReadHWData, IOCTL 0x9C412408
  -> HP WMI GUID
  -> ACPI method 2
```

`HpReadHWData.sys` acts as the WMI forwarder in this path; the lighting modes,
register sequences, and delays are selected in userspace. This supports the
same transport/protocol boundary used by the Linux project.

## Fixed WMI transport envelope

Every MLED register write uses this fixed envelope:

| Field | Value |
|---|---:|
| GUID | `5FB7F034-2C63-45E9-BE91-3D44E2C707E4` |
| SGIN | `0x55434553` |
| COMD | `0x00020009` |
| CMDT | `0x0A` |
| method ID | `2` |
| DSZI | `4` |
| payload | `[reg, value, slave, 0x00]` |

Known DIMM slave addresses are `C0`, `C2`, `C4`, and `C6`. The current kernel
bridge permits only registers `08`, `09`, `20`, `30`, `31`, `32`, and `33`.
That whitelist is sufficient for Off and one-color Static only. The dynamic
registers documented below remain inaccessible through the kernel API.

The corrected four-byte request was **Observed** to drive Intel I801 status
through `0x01 -> 0x41 -> 0x42`. Firmware owns that underlying transaction; the
Linux bridge leaves `i2c_i801` and `spd5118` bound and never accesses I801
registers directly.

## Implemented operations

### Off

For multiple DIMMs, HP begins all transfers in reverse target order, sets
brightness to zero in forward order, then commits in forward order:

```text
reverse targets: 08 53, wait 50 ms after each write
forward targets: 20 00, wait 5 ms after each write
forward targets: 08 44, wait 50 ms after each write
```

### Static color

The staged begin/commit operations surround these per-DIMM settings:

```text
08 53, wait 50 ms
09 00, wait 5 ms
20 brightness, wait 5 ms
30 01, wait 5 ms
31 red, wait 5 ms
32 green, wait 5 ms
33 blue, wait 5 ms
08 44, wait 50 ms
```

Brightness is retained as the protocol byte `0..255`; this project does not
claim that it is perceptually linear or a percentage. The Windows UI uses the
discrete values `0`, `25`, `50`, `75`, and `100` for its brightness choices.

If a write or delay fails after one or more `08:53` begin operations,
userspace retains the original error while issuing best-effort `08:44` writes
to every DIMM whose transfer was opened. A cleanup failure never prevents
cleanup from being attempted on the remaining DIMMs.

## Windows-derived dynamic transaction structure

For the usual target list `[C0, C2, C4, C6]`, HP's FURY DDR5 controller builds
dynamic effects as follows:

1. Begin each target in reverse order (`C6`, `C4`, `C2`, `C0`) with `08=53`,
   waiting 50 ms after every write.
2. Send each mode-setting register to all targets in reverse order, waiting
   5 ms after every write. Settings are sent in the insertion order shown in
   each mode table below.
3. Assign synchronization roles in forward order, waiting 5 ms per write:

   | Slave | `0B` value |
   |---|---:|
   | `C0` | `03` |
   | `C2` | `02` |
   | `C4` | `01` |
   | `C6` | `00` |

4. Commit in forward order with `08=44`, waiting 50 ms after every write,
   followed by one additional 50 ms wait.

The role values depend on target position/count in the Windows controller; the
table above records the normal four-DIMM case and should not be generalized to
arbitrary subsets without first checking the controller logic and hardware.

### Palette encoding

Custom palettes use `30=count`, followed by packed RGB triplets beginning at
register `31`. One color therefore occupies `31..33`; ten colors occupy
`31..4E`. A future palette-capable implementation must expand the kernel
whitelist deliberately for the full bounded range it supports.

The Galaxy Color Cycle palette is fixed to these ten colors, in order:

```text
FF0000 00FF00 FF6400 0000FF EFEF00
800080 006D77 FFC800 FF55FF 3C7DFF
```

Embedded theme data also contains Jungle, Ocean, Volcano, Unicorn, Arcane,
Valorant, OMEN, and HyperX palettes. The current FURY DDR5 UI constructs only
Galaxy as a named theme, so the other shared-data palettes are not evidence of
FURY DDR5 support.

## Windows-derived modes

Register/value rows are listed in the exact order in which HP's controller
adds them to the settings transaction. All values are hexadecimal unless a
table explicitly says otherwise.

### Wave (effect index 0)

Galaxy Wave:

| Order | Register | Value |
|---:|---:|---|
| 1 | `09` | `01` |
| 2 | `0D` | `00` |
| 3 | `0E` | fast `0F`, medium `33`, slow `64` |
| 4 | `20` | brightness |
| 5 | `0C` | right-to-left `02`; left-to-right `01` when selected |

The UI presents right-to-left and outward. The outward branch does not write
`0C`, apparently relying on the mode default. A left-to-right value exists in
the controller switch but is not presented by this UI path.

Custom/non-Galaxy Wave:

| Order | Register | Value |
|---:|---:|---|
| 1 | `09` | `06` |
| 2 | `0D` | `00` |
| 3 | `0E` | fast `32`, medium `64`, slow `96` |
| 4 | `20` | brightness |
| 5 | `26` | `07` |
| 6+ | `30...` | palette count and RGB bytes |
| last | `0C` | direction when applicable |

Galaxy Wave is the smallest dynamic effect to validate next. In addition to
the current whitelist, it needs only `0B`, `0C`, `0D`, and `0E`. Custom Wave
also needs `26` and a palette-sized register range.

### Breathing (effect index 1)

| Order | Register | Fast | Medium | Slow |
|---:|---:|---:|---:|---:|
| 1 | `09` | `03` | `03` | `03` |
| 2 | `16` | `0A` | `1A` | `2A` |
| 3 | `17` | `05` | `0D` | `15` |
| 4 | `18` | `0A` | `1A` | `2A` |
| 5 | `19` | `05` | `0D` | `15` |
| 6 | `1A` | `01` | `03` | `05` |
| 7 | `1B` | `64` | `64` | `64` |
| 8 | `1C` | `32` | `32` | `32` |
| 9 | `1D` | `00` | `00` | `00` |
| 10 | `20` | brightness | brightness | brightness |
| 11 | `0D` | `00` | `00` | `00` |
| 12 | `0E` | `00` | `00` | `00` |
| 13+ | `30...` | palette | palette | palette |

This mode additionally requires `0B`, `0D`, `0E`, `16..1D`, and a bounded
palette register range.

### Blinking (effect index 2)

| Order | Register | Fast | Medium | Slow |
|---:|---:|---:|---:|---:|
| 1 | `09` | `03` | `03` | `03` |
| 2 | `16` | `00` | `00` | `00` |
| 3 | `17` | `06` | `0A` | `10` |
| 4 | `18` | `00` | `00` | `00` |
| 5 | `19` | `00` | `00` | `00` |
| 6 | `1A` | `03` | `05` | `08` |
| 7 | `1B` | `64` | `64` | `64` |
| 8 | `1C` | `40` | `40` | `40` |
| 9 | `1D` | `00` | `00` | `00` |
| 10 | `20` | brightness | brightness | brightness |
| 11 | `0D` | `00` | `00` | `00` |
| 12 | `0E` | `00` | `00` | `00` |
| 13+ | `30...` | palette | palette | palette |

This mode needs the same additional whitelist families as Breathing.

### Color Cycle (effect index 3)

| Order | Register | Value |
|---:|---:|---|
| 1 | `09` | `04` |
| 2-3 | `12`, `13` | 16-bit timer, high byte then low byte |
| 4-5 | `14`, `15` | the same 16-bit timer |
| 6 | `20` | brightness |
| 7 | `0D` | `00` |
| 8+ | `30...` | palette count and RGB bytes |

The timer values are:

| Speed | Decimal | Bytes written to `12..13` and `14..15` |
|---|---:|---|
| Fast | 437 | `01 B5` |
| Medium | 857 | `03 59` |
| Slow | 1312 | `05 20` |

Color Cycle additionally requires `0B`, `0D`, `12..15`, and the complete
bounded palette register range (`30..4E` for the ten-color Galaxy palette).

## Implementation guidance

Dynamic effects belong in userspace. The kernel should continue to expose only
validated single-register MLED writes, session ownership, and capabilities; it
must not acquire an effects engine, timers, palettes, or policy.

The safest incremental implementation is Galaxy Wave:

- expand the kernel whitelist only to `0B..0E`;
- add a userspace protocol operation with the exact reverse-settings,
  forward-role, and forward-commit ordering above;
- unit-test every write and delay without opening the device;
- hardware-test one DIMM at moderate brightness before trying four-DIMM
  synchronization; and
- retain best-effort commit cleanup on every failure path.

Only after hardware validation should a Windows-derived effect be relabeled
as implemented/validated here. Breathing, Blinking, Color Cycle, custom Wave,
and multi-color palettes should remain outside the kernel whitelist until each
has an explicit userspace implementation and test plan.
