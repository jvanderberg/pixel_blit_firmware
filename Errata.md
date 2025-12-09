Errata:
- QSPI_SD1 and QSPI_SD0 reversed
- SCL/SDA ordering - misnamed in silkscreen and backwards.
- Consider pull-ups on select and next
- Board address levels reversed
- Add 12V input protection on TPS63070 tap-off path only: small P-FET reverse polarity,
  TVS diode, inrush limiting. High-current LED pass-through remains unprotected.
  (Hot-plugging killed TPS63070 and input caps during testing)
- IR receiver 3.5mm jack pinout: Board expects Tip=5V, Ring=Data, Sleeve=GND.
  Many IR modules use Tip=Data, Ring=5V. If IR lights up but sends no data,
  use a patch cable that swaps tip and ring. Can power with 3.3V instead of 5V.
- Increase resistor on 12V power LED (too bright). Consider equalizing all three
  power LED resistors for consistent brightness/current.