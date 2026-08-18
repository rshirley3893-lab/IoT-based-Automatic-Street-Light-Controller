# Smart Adaptive Street Light Controller - Dashboard + Firmware

## What's in this package

```
firmware/
  street_light_controller.ino   ← upload this to your Arduino UNO
dashboard/
  index.html                    ← open this in Chrome to run the dashboard
  style.css
  app.js
```

## Hardware Components used for firmware

1. Arduino UNO R3
2. 3x LEDs
3. 3x IR sensors
4. 1 LDR
5. 220 ohm resistors
6. Jumper wires

## 1. Upload the firmware

1. Open `firmware/street_light_controller.ino` in the Arduino IDE.
2. Check `LDR_HIGH_MEANS_NIGHT` near the top — it's already set to match what you confirmed during testing (`true`). Only change it if your wiring changes.
3. Upload as usual. Pin map is unchanged from your tested build:
   - LDR D0 → D5
   - IR1/IR2/IR3 OUT → D2/D3/D4
   - LED1/LED2/LED3 → D9/D10/D11 (PWM)
4. **Close the Arduino IDE's Serial Monitor before opening the dashboard.** Only one program can hold the serial port open at a time — if the Serial Monitor has it, the browser's "Connect Arduino" button will fail to find the port.

## 2. Run the dashboard

1. Open `dashboard/index.html` directly in **Google Chrome, Microsoft Edge, or Brave** (the Web Serial API used to talk to the Arduino only works in Chromium-based browsers — it will not work in Firefox or Safari).
2. Click **Connect Arduino** in the top-right corner.
3. A browser permission dialog will list available serial ports — select the one corresponding to your Arduino UNO and click **Connect**.
4. Once connected, the status dot turns teal and telemetry starts updating roughly 5 times per second.

If nothing happens after connecting, double check the Arduino IDE's Serial Monitor is fully closed (see step 4 above), and that the Arduino is running the uploaded firmware (its onboard LED activity or your LEDs should already be reacting to sensors independent of the dashboard).

## 3. Demo script (matches PRD Section 22)

- **Automatic lighting** — cover the LDR: LEDs move to the night baseline. Uncover: LEDs go fully off.
- **Predictive lighting** — with the LDR covered, trigger IR1 then IR2 in sequence (a couple seconds apart). Watch the *Predictive lighting* page: Zone 3 should pre-illuminate at full brightness before anything is physically in front of it.
- **Traffic intensity** — trigger 1, then 2, then all 3 IR sensors at once and watch the *Traffic intensity* page badge move Low → Medium → High.
- **Emergency mode** — click **Activate emergency mode** on the *Emergency mode* page (or the quick button in the status bar). All three LEDs jump to 100% instantly, regardless of day/night or sensors. Click **Return to automatic** to hand control back.
- **Weather adaptive** — with the LDR covered and no vehicles detected, click through Clear → Normal rain → Heavy rain → Fog on the *Weather adaptive* page and watch the baseline brightness (and physical LEDs) shift between 20% / 50% / 100%.

## Notes

- The Arduino keeps running its automatic logic on its own even if the dashboard is closed or the browser tab is disconnected (per FR-10) — the dashboard is a control/visibility layer, not a required dependency.
- The serial protocol is plain, newline-delimited text exactly as specified in the PRD (Section 14), so you can also watch it directly in the Arduino IDE's Serial Monitor (at 9600 baud) when the dashboard isn't connected, for debugging.
- Brightness percentages shown on the dashboard are read back from the Arduino's own telemetry (not assumed by the browser), so what you see is always what the hardware is actually doing.
