# JC3636K518C_I_YR1 board component

Native ESP-IDF **5.4.3** board driver for the ESP32-S3 side of the user's
JC3636K518C_I_YR1, SKU 10160002 (2633), 360×360 capacitive-touch knob.
This component contains no HA/ESPHome runtime, network service, audio driver,
secret, or Spotify implementation. It does not prove the exact PCB revision,
physical functions, memory population, or commercial product approval.

## Integrate

Add `firmware/components` to the application's `EXTRA_COMPONENT_DIRS`, require
`pw_board` in its main component, and include `pw_board.h`. Only built-in IDF
components are required. Select `esp32s3`. The component itself does not require
PSRAM; its DMA stripe lives in internal RAM. The application must separately
select and verify its actual flash/PSRAM mode.

`pw_board_init()` is a once-per-boot startup call. It resets and initializes
peripherals synchronously; runtime APIs do not perform I2C/SPI transfers or
wait for queues. It leaves the backlight at zero. The application submits an
initial complete frame, waits for that completion outside its input path, then
calls `pw_board_set_brightness()`.

`pw_board_draw_bitmap()` submits a tightly packed native-endian RGB565 region.
Right/bottom bounds are exclusive, so a full image uses `(0, 0, 360, 360)`.
The caller retains the pixel storage, unchanged, until its completion callback.
Every accepted submission completes exactly once. A rejected/full queue
returns an error immediately, transfers no ownership, and has no callback.
The callback runs in the display task, not an ISR; it must not block. A LVGL
adapter may signal flush completion there and must handle submission errors
itself. There are two queued descriptors plus the active transfer; allocate
and retain UI draw buffers accordingly.

The display task converts RGB565 to wire order in one 14,400-byte internal DMA
stripe, sends at most 20 rows per transfer, and waits outside the UI task.
A DMA timeout makes the display unavailable until reboot and rejects later
work through error callbacks; it never reuses an uncertain DMA buffer. Input
and network tasks remain independently schedulable. Queue saturation and IO
errors are exposed in diagnostics.

`pw_board_poll_input()` returns a cached touch sample and takes accumulated
EC1 movement for one UI consumer. Poll every 5–10 ms. The separate input task
samples the two PCNT units every 10 ms; the counters continue during other
work. Both-direction batches are recorded but produce no movement. There is
no ring push switch and EC2 is not added. A PCNT reset-like backward jump is
rejected; unsigned delta arithmetic handles the long-run counter wrap.
The UI owns wake-gesture consumption, acceleration, modal navigation and
haptic selection.

Touch uses bounded 5 ms I2C transactions on its worker; a read failure lasting
100 ms releases a held touch. Coordinates outside the panel and explicit
release events cannot become valid presses. Unknown touch chip IDs remain
unavailable rather than being treated as a supported model. Haptic requests
replace the pending effect; no growing effect queue can delay interaction.

## Pin profile and provenance

| Function | GPIO / bus | Provenance |
| --- | --- | --- |
| ST77916 QSPI | CLK13, CS14, D0–3 15/16/17/18, RESET21, 80 MHz | Upstream panel YAML |
| Backlight | GPIO47, 20 kHz LEDC | Upstream pin, native PWM configuration |
| CST816 S/T/D | SDA11/SCL12, IRQ9, RESET10, address 0x15 | Upstream YAML and CST816 register reference |
| DRV2605 | same I2C, address 0x5A, LRA, library6 | Upstream YAML; TI register reference |
| EC1 left/right | GPIO7/GPIO8, active low, 10 μs filter | Upstream PCNT implementation and recognition document |

The **232 panel initialization commands/delays** in `st77916_init.h` are a
mechanical translation of `esphome/rotaryknob-s3-ui-core.yaml` at commit
`9cc5576c2fd4a9beb56cad24e211957c43aa2c62` in
[Passion-Wave-rotaryknob](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/esphome/rotaryknob-s3-ui-core.yaml).
The EC1 behavior, pin mapping, watch points, pull-ups and filtering are adapted
from [ec1_pcnt_encoder.h](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/esphome/ec1_pcnt_encoder.h)
and `docs/rotary-recognition.md` at that same commit. The upstream MIT notice
is preserved in `UPSTREAM-LICENSE.txt`. ESPHome-specific bindings are removed.

The new driver uses Espressif's installed 5.4.3 `esp_lcd` SPI IO API. QSPI
register writes use opcode 0x02; color writes use opcode 0x32 and RAMWR 0x2C.
Each stripe explicitly sets its window, avoiding dependence on continued
write command behavior. RGB order and inversion follow the upstream profile.
Touch register interpretation was checked against the
[ESPHome CST816 component](https://github.com/esphome/esphome/tree/dev/esphome/components/cst816/touchscreen)
(register facts only; that implementation is not copied). Haptic register
facts were checked against [TI's DRV2605L data sheet](https://www.ti.com/lit/ds/symlink/drv2605l.pdf).
Existing gain/calibration bits are preserved; no voltage increase, continuous
RTP drive, or speculative actuator calibration is applied.

## Acceptance status

An isolated S3 application is used to compile and link this component with
IDF 5.4.3. Exact build result is recorded in the implementation handoff. This
is a compiler check, not physical acceptance.

Still required on the user's board: RGB/inversion/rotation and all round-screen
edges; touch alignment/hold/release; direction and exact detent count during
fast turns and mixed UI/WLAN load; idle common-mode rejection; optional haptic
presence and actuator suitability; display-off with active website; heap,
DMA, stack and latency measurements. Audio, DAC mute, I2S mux, power control,
companion reset/boot and battery ADC remain outside this component. In
particular no uncertain audio/strapping pin is driven.
