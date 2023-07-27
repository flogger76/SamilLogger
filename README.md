# SamilLogger for Samil SolarLake inverters

This is a fork of https://github.com/vk2tds/SamilLogger

The code has been changed to cooperate with SolarLake TL-PM series inverters.
The Application has been tested with SL 10000TL-PM inverter/
Example of communication with the inverter:

55 AA 00 00 00 00 00 00 00 00 FF - Software -> Inverter   Request Serial Number from any inverter without an address
55 AA 00 00 00 00 00 80 0A 54 30 34 31 34 34 59 30 39 37 03 D3 - Inverter -> Software   Provides a serial number to the Software
55 AA 00 FF 00 00 00 00 01 15 02 14 - Inverter -> Software   Second answer from inverter ???
55 AA 00 00 00 00 00 01 0B 54 30 34 31 34 34 59 30 39 37 01 03 56 - Software -> Inverter   Request Logs into the inverter when followed by the Serial Number in the Data field
55 AA 00 01 00 00 00 81 01 06 01 88 - Inverter -> Software  Inverter allocates itself an address and replies
55 aa 00 00 00 01 01 03 00 01 04 - Software -> Inverter  Request software version and inverter specifications
55 aa 00 01 00 00 01 83 3c 32 20 31 30 30 30 30 56 32 2e 36 31 53 4c 20 20 31 30 30 30 30 54 4c 2d 50 4d 20 20 53 61 6d 69 6c 50 6f 77 65 72 00 20 20 20 20 20 54 30 34 31 34 34 59 30 39 37 00 00 00 00 00 00 0e 87 - Inverter -> Software  Supply inverter software version and inverter specifications
55 aa 00 00 00 01 01 02 00 01 03 - Software -> Inverter  Request PV data
55 aa 00 01 00 00 01 82 48 02 30 0f 3e 08 ff 00 13 00 12 00 00 9d d5 00 00 08 20 04 57 00 01 12 18 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 02 ec 01 a6 02 26 00 12 09 6b 13 85 00 11 09 78 13 85 00 12 09 34 13 85 0a 82 - Inverter -> Software   PV Data




# Samil SolarRiver Inverter Logger based on ESP8266

This ESP8266 firmware enables you to read information from a Samil solar inverter through it's RS485 bus.
Sending information to a MQTT broker is supported, as well as uploading information to [PVoutput](https://pvoutput.org/). This is a fork of https://github.com/jantenhove/GoodWeLogger

## Requirements
  - Samil inverter with RS485 connector
  - RS485 converter (can be found on sites like AliExpress, search for *SCM TTL to RS485 Adapter 485 to UART Serial Port 3.3V 5V Level Converter Module*)  
  - ESP8266 (like NodeMCU or Wemos D1 mini)
  - Computer with Arduino IDE installed

## Flashing firmware
 - To flash this firmware, you will need to install the Arduino IDE and configure it for the board you are using (NodeMCU / Wemos)
 - You will also need the following libraries
   - ['Time' library](https://github.com/PaulStoffregen/Time)
   - [NTPClient](https://github.com/arduino-libraries/NTPClient)
   - [PubSubClient](https://github.com/knolleary/pubsubclient)
   Place these into the `libraries` folder belonging to Arduino on your system.
 - Clone/download this repository
 - Rename the `Settings.example.h` to `Settings.h` and configure it to match your preferred settings
 - Compile and upload the firmware to your ESP8266
 - Diagnostics information is sent over serial at 115200 baud *(wifi status, MQTT status, inverter connection status)*

## Connecting hardware
The RS485 connector on the inverter is located at the bottom of the inverter. It is hidden behind a small metal plate.
There will be two (2) RJ11, 4 pin sockets. Only one RJ11 (left) is required and the pinout is as follows (left to right):

Pin | Function
--- | ---
1 | TX+
2 | TX-
3 | RX+
4 | RX-

Connection from the TTL-RS485 module:

TTL RS485 | Connection
--- | ---
A+ | TX+ and RX+
B+ | TX- and RX-

***Other models might use a different method of connecting. Consult your inverter manual.***

Connect the RS485 converter to your ESP8266 like this:

RS485 converter | ESP8266
--- | ---
GND | G / GND
RXD | D1
TXD | D2
VCC | 5V / 3V3

*(`D1` (receive) and `D2` (transmit) can be configured to different pins in `Settings.h`)*. It might look weird to connect `RXD` of the module to the receive pin of the ESP8266, but this is how the RS485 converter is labeled.

## MQTT
Subscribe to the `samil/` topic in your MQTT client. Information will be posted there and will look like this:
```
samil/93600DVA295R148/vpv1 242.6
samil/93600DVA295R148/vpv2 235.7
samil/93600DVA295R148/ipv1 4.9
samil/93600DVA295R148/ipv2 5.5
samil/93600DVA295R148/vac1 240.6
samil/93600DVA295R148/iac1 10.7
samil/93600DVA295R148/fac1 49.99
samil/93600DVA295R148/pac 2582
samil/93600DVA295R148/temp 38.6
samil/93600DVA295R148/eday 6.40
samil/93600DVA295R148/workmode 1
samil/93600DVA295R148/online 1
```
Field | Description | Unit
--- | --- | ---
vpv1 | Voltage of first string of solarpanels | V
vpv2 | Voltage of second string of solarpanels | V
ipv1 | Current of first string of solarpanels | A
ipv2 | Current of second string of solarpanels | A
vac1 | Voltage mains side inverter | V
iac1 | Current mains side inverter | A
fac1 | Frequency (Hz) mains side inverter | Hz
pac | Current power production in Watt | W
temp | Internal temperature of inverter | &deg;C
eday | Energy produced today | kWh
workmode | Undocumented parameter. Default=1 | binary
online | Inverter status (1=on, 0=off) | binary

## PVoutput
When you have your PVoutput *API key* and *System ID* configured correctly in `Settings.h`, production data from the inverter will be uploaded to PVoutput every 5 minutes *(interval is configurable in `Settings.h`, but don't go lower than the minimal interval of every 5 minutes as specified by PVoutput)*.
When multiple inverters are connected, by daisy-chaining the RS485 cable, only the production data of the first inverter will be uploaded.

For the PVoutput upload function to work, it is important that the ESP8266 has access to the internet. 
Apart from connections being made to PVoutput, you will also see that the ESP8266 talks with pool.ntp.org on a regular interval. This is done to retrieve the current time, which is needed to post data to PVoutput.

If you plan to use only MQTT, internet access for the ESP8266 is not needed.
