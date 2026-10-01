# Parts list

Prices are approximate (US, 2026) and most parts come in multipacks, so one order covers spares. The links are examples; any part with the same chip or rating works.

## Electronics

| Qty | Part | Notes | Example link |
|---|---|---|---|
| 1 | ESP32 dev board, ESP-WROOM-32 (30- or 38-pin) | Must be the original ESP32, not an S3 or C3 | [ELEGOO 3-pack](https://www.amazon.com/ELEGOO-ESP-WROOM-32-Development-Bluetooth-Microcontroller/dp/B0D8T53CQ5) |
| 1 | INMP441 I2S MEMS microphone | Runs on 3.3 V only | [AITRIP 3-pack](https://www.amazon.com/AITRIPAITRIP-AITRIP-Omnidirectional-Microphone-Interface/dp/B0972XP1YS) |
| 1–2 | MAX98357A I2S 3 W amplifier | Two amps + two speakers is louder | [SparkFun](https://www.amazon.com/SparkFun-PID-14809-Audio-Breakout/dp/B07HWVPSCG) or [generic 2-pack](https://www.amazon.com/MAX98357-MAX98357A-Amplifier-Breakout-Raspberry/dp/B0H5QD594L) |
| 1–2 | 4 Ω 3 W speaker, 40 mm or 2" | One per amp | [Gikfun 40 mm 2-pack](https://www.amazon.com/Gikfun-Speaker-Stereo-Loudspeaker-Arduino/dp/B01LN8ONG4) or [Gikfun 2" 2-pack](https://www.amazon.com/Gikfun-Speaker-Stereo-Loudspeaker-Arduino/dp/B01CHYIU26) |
| 4 | 5 mm LEDs: blue, yellow, red, green | Diffused LEDs are easier to see from an angle | Any assortment |
| 4 | 330 Ω resistor | One per LED | Any assortment |
| 1 | 470–1000 µF electrolytic capacitor, 10 V or higher | Across 5 V and GND near the amps | Any assortment |

## Power

| Qty | Part | Notes | Example link |
|---|---|---|---|
| 1 | USB-C power breakout with 5.1 kΩ CC resistors | Without the CC resistors, USB-C chargers supply no power | [Lonely Binary 12-pack](https://www.amazon.com/Lonely-12-Pack-Breakout-Board-Resistor/dp/B0GTQH9XKP) or Adafruit #4090 / #5993 |
| 1 | Panel-mount rocker or toggle switch, 3 A or more | Switches the 5 V line | KCD1-style mini rocker |
| 1 | USB charger or power bank, 2 A or more | Two amps at full volume draw short bursts over 1 A | |

## Prototyping and assembly

| Qty | Part | Example link |
|---|---|---|
| 1 | Solderless breadboard(s) | [ELEGOO 4-pack](https://www.amazon.com/ELEGOO-Breadboard-Solderless-Breadboards-Electronics/dp/B0CXF1B6GB) |
| 1 | Dupont jumper wires (M-M, M-F, F-F) | [ELEGOO 120 pcs](https://amazon.com/ELEGOO-Breadbord-Jumper-Wires/dp/B01EV70C78) |
| — | Soldering iron and solder | The mic and amp boards usually ship with loose header pins |
| — | 3D-printed case | See [`case/`](case/) |

## Optional

| Part | Why |
|---|---|
| 100 kΩ resistor (per amp) | GAIN pin to GND through 100 kΩ sets the amp to 15 dB, its loudest setting |
