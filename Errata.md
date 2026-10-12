Errata:
- QSPI_SD1 and QSPI_SD0 reversed
- SCL/SDA ordering - misnamed in silkscreen and backwards.
- Consider pull-ups on select and next
- Board address component removed - voltage divider levels were unreliable
- Add 12V input protection on TPS63070 tap-off path only: small P-FET reverse polarity,
  TVS diode, inrush limiting. High-current LED pass-through remains unprotected.
  (Hot-plugging killed TPS63070 and input caps during testing)
- IR receiver 3.5mm jack pinout: Board expects Tip=5V, Ring=Data, Sleeve=GND.
  Many IR modules use Tip=Data, Ring=5V. If IR lights up but sends no data,
  use a patch cable that swaps tip and ring. Can power with 3.3V instead of 5V.
- IR receiver jack insertion causes 5V brownout: Tip shorts to sleeve during
  insertion. Add 220Ω series resistor on 5V to limit inrush current (~23mA max).
  IR receivers draw <5mA so voltage drop is negligible during normal operation.
- Increase resistor on 12V power LED (too bright). Consider equalizing all three
  power LED resistors for consistent brightness/current.
- Add power distribution fusing (7 blade fuse positions total):
  - 4x terminal block fuses: One fuse per terminal block, sized for that block's load
  - 3x inter-segment fuses: High-value (25-30A) "soft jumpers" between power plane
    segments. Install for single-PSU operation, remove to isolate segments for
    multi-PSU setups. Provides 1/2/4 PSU configuration flexibility.
  - Parts: Use 2x Keystone 3544-2 clips per position (LCSC C3207000, ~$0.50 each).
    Mini blade fuse terminal spacing: 7.7mm center-to-center, blade width 2.8mm.
- L2 inductor (MLI2012F-1R5KT) is wrong part: 50mA signal inductor instead of power
  inductor. Causes 20kHz oscillation on 5V rail, 210mA idle draw at 12V (should be
  ~50mA), and 93°C operating temperature. TPS63070 requires inductor with Isat > 3.6A.
  Fix: Replace with Bourns SRP4020TA-1R5M (1.5µH, 4.5A, 4x4mm) or SRP3012TA-1R5M
  (1.5µH, 4.0A, 3x3mm). Bodge by removing L2 and wiring in replacement with short leads.
- DSLVDS1047PWR (LVDS driver, C2671205) out of stock at JLCPCB.
  Replacement: DS90LV047ATMTCX/NOPB (C87097). Drop-in, same TSSOP-16 pinout,
  same 3.0–3.6V supply, 400Mbps. Compatible with existing DSLVDS1048 receiver.