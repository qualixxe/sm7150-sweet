
## What the working firmware actually is

The device tree carried by the surya firmware that boots on courbet, and by the
sweet firmware that also boots, both identify as:

```
model      = Qualcomm Technologies, Inc. SDMMAGPIE SoC
compatible = qcom,sdmmagpie
qcom,board-id = <empty>
```

`SDMMAGPIE` is **SM8150, Snapdragon 855**, not the SM7325 of courbet. The file in
this repository called `courbet.dtb` is a generic SM8150 SoC tree with an empty
board id; it is not a courbet tree.

That is why the sweet firmware runs on courbet at all: both firmwares target the
same SoC generation, so they are interchangeable. An earlier conclusion in this
project that "they share a SoC because both are SM7325" was wrong - it was read
off filenames rather than out of the trees. `sweet.dtb` carries
`qcom,sdmmagpie-qrd`, a Qualcomm reference design, which is the likely reason
the buttons and display come up without a device-specific description.

Consequences worth remembering:

- touch is never driven by the firmware; it only matters once Windows runs, and
  it needs a driver plus a real courbet tree. The DSDT here describes sweet's
  Goodix GT9896 on I2C 0x5D.
- `7150.dtb` is not related to SM7150 either. It identifies as `qcom,apq8016-sbc`.

## Touchscreen in the firmware

Not expected and not implemented. The firmware has no touch input driver, so a
working GUI means display and input buttons are fine. Touch is a Windows-side
question.