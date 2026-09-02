# Watara Supervision → Composite Video (RCA) mod by AndyGaming90

Convert the original Watara Supervision LCD bus into a standard NTSC composite-video signal using a **Raspberry Pi Pico 2 (RP2350)**.

This project is based on the LCD-capture work from [DutchMaker/Supervision-LCD-v2](https://github.com/DutchMaker/Supervision-LCD-v2) and the NTSC video output work from [xrip/rp2040-ntsc-tv](https://github.com/xrip/rp2040-ntsc-tv).

The goal of this version is **not** to replace the Supervision LCD with an IPS panel. Instead, the Pico captures the real LCD signals from the console and outputs them as **composite video over RCA**, suitable for a CRT or other display with composite input.

---

## Features

- Captures the real Watara Supervision LCD bus
- Raspberry Pi **Pico 2 / RP2350**
- NTSC composite output on **GP15**
- 160×160 Supervision framebuffer
- Scaled output for a 320×240 NTSC framebuffer
- Black border around the Supervision image
- Double-buffered NTSC output
- Pixel-clock glitch filtering for reliable capture on RP2350
- Frame-polarity filtering
- Protection against framebuffer overflow
- Four Supervision grayscale/green levels plus a separate black border color

---

## Hardware

### Required

- Watara Supervision
- Raspberry Pi Pico 2
- RCA female connector
- **75 Ω resistor**
- **560 pF ceramic capacitor** (`561`)
- Wire
- Common ground between the Supervision, Pico and RCA output

A 1/4 W resistor is more than sufficient for the 75 Ω video resistor.

### Composite output circuit

```text
Pico GP15
   |
  75 Ω
   |
   +--------------------> RCA center pin (video)
   |
 560 pF
   |
  GND

Pico GND ---------------> RCA shield
Supervision GND --------> Pico GND
```

The common ground is essential. Without a shared ground, the Pico does not have a reliable reference for the Supervision LCD signals.

---

## Supervision → Pico connections

| Supervision signal | LCD connector pin | Pico GPIO | Description |
|---|---:|---:|---|
| GND | 1 | GND | Common ground |
| DATA0 | 2 | GP16 | LCD data line 0 |
| DATA1 | 3 | GP17 | LCD data line 1 |
| DATA2 | 4 | GP18 | LCD data line 2 |
| DATA3 | 5 | GP19 | LCD data line 3 |
| PIXEL_CLOCK | 6 | GP20 | Pixel clock |
| FRAME_POLARITY | 9 | GP21 | Field/frame polarity |

The current capture code does **not** use the Line Latch or Frame Latch pins.

On the hardware tested during development, attaching the Pico directly to the Frame Latch line caused instability/stuttering, so pins 7 and 8 are intentionally left disconnected.

---

## Composite video output

| Signal | Pico | Connection |
|---|---:|---|
| Composite video | GP15 | Through 75 Ω resistor to RCA center |
| Video ground | GND | RCA shield |

GP15 is intentional. The NTSC implementation drives the **PWM B channel**, and GP15 maps correctly to the required PWM channel on the Pico 2.

---

## Supervision LCD connector pinout

```text
    1  2  3  4  5  6  7  8  9  10 11 12
    |  |  |  |  |  |  |  |  |  |  |  |
.---|--|--|--|--|--|--|--|--|--|--|--|---.
|   o  o  o  o  o  o  o  o  o  x  o  x   |
|            <LCD connector>                |
|                                           |
|               Supervision                 |
`-------------------------------------------'
```

1. Ground
2. Data 0
3. Data 1
4. Data 2
5. Data 3
6. Pixel clock
7. Line latch — unused in this project
8. Frame latch — unused in this project
9. Frame polarity
10. Power control / unused
11. LCD supply rail
12. Unused

---

## Why the capture code differs from the original RP2040 version

The DutchMaker capture code was originally written for the RP2040. On a Pico 2 / RP2350, directly polling the pixel-clock line produced extra apparent clock edges on the tested hardware.

The working solution uses a small software filter:

- the pixel clock must be sampled high twice before it is accepted;
- the high counter is allowed to saturate rather than immediately retrigger;
- frame polarity must also remain changed for multiple samples before a new field is accepted;
- pixel data is only accepted while the currently sampled polarity still matches the confirmed field polarity.

This greatly reduces fragmented horizontal image data.

The capture also stops writing after **6400 accepted pixel clocks per field**:

```text
160 × 160 pixels
4 pixels per pixel-clock
160 × 160 / 4 = 6400 clocks
```

This prevents the framebuffer pointer from running past the 160×160 buffer if a noisy clock signal is detected.

---

## Multicore synchronization

Capture runs on one core and NTSC rendering runs on the other.

The shared `sync` variable must be declared `volatile` in both its definition and its external declaration. Without this, optimized multicore builds can fail to observe changes made by the other core.

Example:

```c
volatile uint8_t sync = 0;
```

and in the header:

```c
extern volatile uint8_t sync;
```

---

## NTSC output

The NTSC backend runs the RP2350 at **315 MHz**.

This frequency is intentional: it is chosen around the NTSC color-subcarrier timing used by the video generator. Do not reduce the system clock without also reworking the NTSC timing.

The implementation uses:

- PWM output on GP15
- DMA-driven scanline output
- a 320×240 framebuffer
- two NTSC framebuffers to avoid rendering into the same buffer that is currently being scanned out

The Supervision 160×160 image is scaled before being copied into the NTSC framebuffer. The current build uses an enlarged image with a black border around it; the scale and X/Y position can be adjusted in `render_core()`.

---

## Palette

`ntsc_set_color()` uses the argument order:

```text
index, blue, red, green
```

This is intentionally **B, R, G**, not conventional RGB.

The project uses palette entries 0–3 for the Supervision shades and palette entry 4 for the black border.

---

## Recommended video components

Use:

```text
75 Ω resistor
560 pF ceramic capacitor (code 561)
```

If a 75 Ω resistor is temporarily unavailable, useful near-equivalents include:

```text
82 Ω || 820 Ω ≈ 74.5 Ω
100 Ω || 330 Ω ≈ 76.7 Ω
```

where `||` means the two resistors are connected in parallel.

A 100 Ω resistor by itself can produce a noticeably weaker composite signal and may change how some CRTs lock to or display the signal.

---

## Power

The Pico and Supervision must share ground.

For development, powering the Pico over USB is the simplest option.

If the finished console is powered from one regulated supply, keep the Pico within its allowed input-voltage range. A regulated **5 V supply** can be used for the Pico USB/VBUS input while the Supervision is also powered from a suitable 5 V source, provided polarity and current capability are correct.

Do **not** feed the Pico from an unregulated 6 V source through only a series resistor. The Pico current is not constant, so a resistor is not a voltage regulator.

---

## IDE / build setup

Development was done with:

- Visual Studio Code
- Official Raspberry Pi Pico extension
- Raspberry Pi Pico SDK 2.3.x
- Board target: `pico2`

Required Pico SDK libraries include:

```cmake
pico_stdlib
pico_multicore
hardware_pwm
hardware_dma
```

Example build command on Windows:

```powershell
& "$env:USERPROFILE\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" --build build
```

Do not add a separate `ntsc-tv.c` to the build when using the self-contained `ntsc-tv-out.h` version used by this project.

---

## Troubleshooting

### Mostly green / corrupt image

Check the common ground first:

```text
Supervision GND ↔ Pico GND ↔ RCA shield
```

A missing common ground can produce severe corruption even though some image fragments remain visible.

### Horizontal fragments / broken capture

Check:

- GP20 pixel-clock connection
- GP21 frame-polarity connection
- the RP2350 pixel-clock filter in `capture_data()`
- the 6400-clock write limit
- `sync` is declared `volatile`

### Stable image but soft colors / color bleeding

That is often a limitation of composite video itself. A CRT generally displays the signal more naturally than a modern flatscreen, which may deinterlace, sharpen and scale the 240p-like signal.

### Controls behave differently when RCA audio cables are connected

That is **not normal** and usually indicates a ground/return-path wiring problem. Check that RCA center pins and shields are wired correctly and that the controller board has a proper ground connection independent of the television/audio cables.

---

## Credits

This project builds on the work of:

- [DutchMaker/Supervision-LCD-v2](https://github.com/DutchMaker/Supervision-LCD-v2) — Supervision LCD capture
- [xrip/watara-supervision-lcd](https://github.com/xrip/watara-supervision-lcd) — earlier Supervision capture work
- [xrip/rp2040-ntsc-tv](https://github.com/xrip/rp2040-ntsc-tv) — RP2040/Pico NTSC video output

Thanks to everyone who has documented and reverse engineered the Watara Supervision hardware.
