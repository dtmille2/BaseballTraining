# Wi-Fi QR code

Scanning this code with a phone camera offers to join the open **Baseball Timer** network. The control page then opens automatically.

Contents: `WIFI:T:nopass;S:Baseball Timer;;`

| File | Use |
|---|---|
| `wifi_qr.svg` | Black on white, for test prints on paper |
| `wifi_qr_dark_only.svg` | Dark squares only, for embossing or inlaying into a light case surface |

## Size

- QR pattern: 50 × 50 mm, 29 × 29 squares of 1.72 mm
- With the required white border: 63.8 × 63.8 mm. Keep the border plain and light-colored.

## Printing tips

- Use two colors (dark on light). A single-color raised code often won't scan.
- Printing the code face-down on the build plate gives the flattest, crispest result.
- Use a matte filament to avoid glare.
- Print it on paper at 100% scale and test it with an iPhone and an Android phone before printing the case.

If you change the network name or add a password in the firmware (`AP_SSID`, `AP_PASS`), generate a new QR code to match.
