Errata:
- QSPI_SD1 and QSPI_SD0 reversed
- SCL/SDA ordering - misnamed in silkscreen and backwards.
- Consider pull-ups on select and next
- Board address levels reversed
- Add 12V input protection on TPS63070 tap-off path only: small P-FET reverse polarity,
  TVS diode, inrush limiting. High-current LED pass-through remains unprotected.
  (Hot-plugging killed TPS63070 and input caps during testing)