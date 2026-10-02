# Weather screensaver source collage

`weather-condition-collage-source.png` is the original user-provided visual
reference for the photographic weather screensaver. The production-ready,
display-sized files live in [`states/`](https://github.com/passion-wave/Passion-Wave-rotaryknob/tree/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/assets/screensaver/states).

Technical properties:

- 1254 x 1254 pixels;
- RGB PNG without alpha;
- three by three circular motifs with white gutters;
- SHA-256:
  `ba3f8ca1326ef007e80532cb4bca1f489833d78e72de928992568fab192b6408`.

The collage remains stored unchanged as source evidence. Eight approved motifs
were cropped from it; seven missing motifs were generated with OpenAI's built-in
image-generation workflow using the collage as the visual reference. All
production files are 368 x 368 JPEGs and are compiled into the ESP32-S3
firmware as RGB565 images. They are center-clipped to the 360 x 360 panel with
four pixels of static overscan on every edge. Runtime image scaling is forbidden
because its RGB565 transform produced colored vertical edge columns on hardware.
`states/SHA256SUMS` makes the complete set reproducible.

## Complete state mapping

| Home Assistant state | File | Source |
|---|---|---|
| `clear-night` | `states/clear-night.jpg` | generated completion |
| `cloudy` | `states/cloudy.jpg` | source collage R1C3 |
| `exceptional` | `states/exceptional.jpg` | generated completion |
| `fog` | `states/fog.jpg` | source collage R3C1 |
| `hail` | `states/hail.jpg` | generated completion |
| `lightning` | `states/lightning.jpg` | source collage R2C3 |
| `lightning-rainy` | `states/lightning-rainy.jpg` | generated completion |
| `partlycloudy` | `states/partlycloudy.jpg` | source collage R1C2 |
| `pouring` | `states/pouring.jpg` | source collage R2C2 |
| `rainy` | `states/rainy.jpg` | source collage R2C1 |
| `snowy` | `states/snowy.jpg` | source collage R3C2 |
| `snowy-rainy` | `states/snowy-rainy.jpg` | generated completion |
| `sunny` | `states/sunny.jpg` | source collage R1C1 |
| `windy` | `states/windy.jpg` | generated completion |
| `windy-variant` | `states/windy-variant.jpg` | generated completion |

Unknown or empty values deliberately fall back to `partlycloudy`.

## Runtime behavior

- The ESP32 bridge transports the Home Assistant condition as before.
- The S3 maps the condition locally and swaps the already compiled image; there
  is no HTTP request and therefore no loading state.
- Every image already contains its 368 x 368 overscan at build time. LVGL
  center-clips it without scaling or antialias transformation.
- A restrained dark LVGL scrim keeps clock hands and status text readable.
- Clock contrast is selected from the fixed `Smoked Aqua` benchmark palette
  without runtime image analysis. Dark photographs use warm white `#F2F1EE`
  and cool grey `#899297`; light photographs use near-black `#111519` and
  graphite grey `#596268`. The centre and second dots follow the minute hand.
- The old vector weather objects remain allocated but hidden; the photographic
  state mapping is the active beta.12 screensaver.
- Only the S3 image changes. Encoder, touch, Home Assistant and OTA paths remain
  unchanged.
