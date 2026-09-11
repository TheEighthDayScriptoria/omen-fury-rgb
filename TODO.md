# TODO

- Validate the bridge and CLI on the original HP OMEN 45L test machine.
- Confirm static multi-DIMM staging against a fresh Windows trace.
- Gather DMI identifiers and firmware versions without broadening detection.
- Add DKMS or akmods packaging after hardware validation.
- Discuss a standard LED-class or platform interface with Linux maintainers.
- Implement an OpenRGB detector, controller, and `RGBController` behind a
  transport abstraction; do not make OpenRGB depend permanently on this ABI.
- Replace the experimental ioctl transport when a mainline interface exists.
- Add hardware modes only after each register sequence is independently
  captured and validated.
