# ESP3266 EnergyMeter

This sketch reads energy values by counting **S0**-impulses from energy meters, reading out Eastron **SDM** devices via RS-485 Modbus and electric meters via optical **SML** interface (common in Germany).

This software was initially written for ESP8266 on Wemos D1 mini/pro with Arduino IDE and tested with S0 impulses from common energy meters, Modbus communication with Eastron SDM120M and SDM220M, and SML messages sent via optical infrared interface by an Itron OpenWay 3.HZ.



## S0

The optocoupler from a common energy meter can be used to trigger interrupts on the ESP8266's GPIO pins. The Wemos D1 mini/pro board easily fits into a small DIN rail-mounted housing.

The negative output needs to be connected to GND and the positive output can be directly connected to an input GPIO pin with enabled pull-up.
This sketch uses `D1` and `D2` for S0-impulses from up to two devices.



## SDM

Certain Eastron SDM devices are equipped with a Modbus interface which allows reading of several parameters like power and energy via Modbus registers. Together with a MAX3485 chip the ESP8266 is able to join this RS-485 bus and SoftwareSerial can be used to exchange Modbus messages.

The MAX3485 operates at 3.3V and common MAX3485 PCBs can be connected to the ESP8266 as seen on this image with A/B wired to RS-485 interface of the Eastron SDM.

![EnergyMeter SDM Photo](img/EnergyMeter-sdm.jpg?raw=true)

This sketch uses `D6` for TX and `D7` for RX towards the RS-485 bus.
Double-check RX/TX direction as some PCBs might be labeled with RX/TX as seen from the RS-485 bus (and not from the microcontroller). RX means RO (pin 1) and TX DI (pin 4) on the MAX3485.
`D0` is pulled high during data transmission which needs to be connected to the DE/RE pins (2+3) on the MAX3485.



## SML

Modern electricity meters installed in Germany are often equipped with an optical information interface transmitting meassured values encoded in SML messages via an infrared diode. Sometimes this interface must be unlocked with a PIN which can be requested from local electricity provider.

For the circuit a NPN-transistor (BC 547) with emitter connected to GND, collector connected to GPIO pin and pulled to VCC (3.3V) with a 1K resistor and finally a photo diode (SFH 203 FA) between base (anode) and collector (cathode) is required.
This sketch uses `D5` for the photo transistor (collector of the transistor and cathode of the photo diode).

Soldered together this perfectly fits into a small plastic case (Strapubox 2043) which can be mounted on the electricity meter.

![EnergyMeter SML Board](img/EnergyMeter-sml-board.jpg?raw=true)
![EnergyMeter SML Case](img/EnergyMeter-sml-case.jpg?raw=true)
![EnergyMeter SML Photo](img/EnergyMeter-sml.jpg?raw=true)



## HTTP

The gathered values can be retrieved via HTTP from the ESP8266.

URL                | Content
------------------ | ----------------------------
/                  | HTML Index Document
/json              | JSON Document
/s0/<n>            | S0 channel (n >= 1)
/s0/<n>/clr        | Clear Meter Counters
/s0/<n>/mtr        | Current Meter Value (Wh)
/s0/<n>/age        | Age of last meassurement (s)
/s0/<n>/pwr        | Current Power [W]
/s0/<n>/pwr?avg=n  | Average power [W] of last n meassurements
/sdm/<n>           | SDM device (n >= 1)
/sdm/<n>/imp       | Import Meter [Wh]
/sdm/<n>/exp       | Export Meter [Wh]
/sdm/<n>/sum       | Total consumption (import - export) [Wh]
/sdm/<n>/pwr       | Current Power [W]
/sdm/<n>/pwr?avg=n | Average power [W] of last n meassurements
/sml               | SML Meter
/sml/imp           | Import Meter [Wh]
/sml/exp           | Export Meter [Wh]
/sml/sum           | Total consumption (import - export) [Wh]
/sml/pwr           | Current Power [W]
/sml/pwr?avg=n     | Average power of last n seconds
/update            | OTA Firmware Update



## License

Copyright (c) 2021 David Fröhlich
See [LICENSE](LICENSE)
