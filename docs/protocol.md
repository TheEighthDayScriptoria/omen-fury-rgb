# Validated protocol subset

Every operation uses the fixed WMI envelope below:

| Field | Value |
|---|---:|
| GUID | `5FB7F034-2C63-45E9-BE91-3D44E2C707E4` |
| SGIN | `0x55434553` |
| COMD | `0x00020009` |
| CMDT | `0x0A` |
| method ID | `2` |
| DSZI | `4` |
| payload | `[reg, value, slave, 0x00]` |

Allowed slaves are `C0`, `C2`, `C4`, and `C6`. Allowed registers are `08`,
`09`, `20`, `30`, `31`, `32`, and `33`.

## Off

For multiple DIMMs, HP begins all transfers in reverse target order, sets
brightness to zero in forward order, then commits in forward order:

```text
reverse targets: 08 53, wait 50 ms
forward targets: 20 00, wait 5 ms
forward targets: 08 44, wait 50 ms
```

## Static color

The same staged begin/commit ordering surrounds the per-DIMM static settings:

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

Brightness is kept as the validated protocol byte (`0..255`); the project does
not yet claim that this byte is perceptually linear or a percentage.
