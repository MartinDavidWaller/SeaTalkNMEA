# SeaTalkNMEA

SeaTalk is a simple networking interface used by Raymarine to link their range of marine equipment 
together so that data can be shared across all devices. Technology wise it is best described in a document 
written by Thomas Knauf found here:
<a href="http://www.thomasknauf.de/seatalk.htm">SeaTalk Technical Reference</a>.

## Hardware

The project is designed to read SeaTalk data and convert it to standard 
<a href="https://en.wikipedia.org/wiki/NMEA_0183">NMEA 0183</a> data.
Such devices are available commercially but can be very expensive. 
The one manufactured by <a href="https://digitalyacht.co.uk/">Digital YACHT</a> is sold for £150 whereas
the solution here can built for £10 - £20.

It is based around the Arduino Pro Micro. This device was chosen as it provides two
hardware serial ports. One port is accessed via the USB connection and the second
is available on the board. The intention being to read the SeaTalk data via the 
on board serial port and make the NMEA data available via the USB.
 
<img height="400" src="/Pictures/IMG_0611.JPG" />
 
SeaTalk uses a 12 volt signal line. To interface this
to the Arduino it must be converted to TTL level, this can be done using the
circuit published by <a href="http://berreizeta.blogspot.com/2016/09/seatalk-arduino-interface-5v-ttl-for_19.html">Berreizeta</a>:

<img src="/Pictures/SeaTalk_12v_TTL.JPG" />
 
This can easily be built on Veroboard and the whole device assembled as:
 
<img height="400" src="/Pictures/IMG_0612.JPG" />

The GND is connected to the Arduino GND pin and the DATA is connected to the Arduino RXI pin.
 
## Software

SeaTalk sends 9 data bits for each transmitted character, 11 including start and stop bits. This is impossible to process with the usual Arduino hardware serial support so
use is made of a library supplied by <a href="https://forum.arduino.cc/u/nickgammon">Nick Gammon</a>. There is a post on the Arduino forum 
<a href="https://forum.arduino.cc/t/mods-to-hardwareserial-to-handle-9-bit-data/89447">Mods to HardwareSerial to handle 9-bit data</a> that
explains more. The most recent version of this HarwareSerial code can be found in the Source folder.

By default, the NMEA talker ID generated is "GP". This can be changed by talking to the device via a simple terminal emulator such as
<a href="https://www.putty.org/">PuTTY</a>, and sending it the Tcc command where the two characters cc represent the required talk ID. 
This is stored in EEPROM so it only has to be done once.

A debug mode is also supported. This can be turned on or off using the DT or DF command. If true proprietary NMEA sentences are 
generated that show the raw SeaTalk data that has been received.

Typical sentences generated, with debug enabled, look like:
 
<pre>
 
 $PIMSST1,D,20,41,0,0*5e
 $GPVHW,,,,,0.0,N,0.0,K,*77
 $PIMSST1,D,0,42,64,0,0*41
 $GPDBT,0.0,f,0.0,M,0.0,F*06
 $GPDPT,0.0,0*49
 
</pre>

References:
    
- The only, and best, <a href="http://www.thomasknauf.de/seatalk.htm">SeaTalk Technical Reference</a> to be found on the internet.
- A 12v to TTL interface published by <a href="http://berreizeta.blogspot.com/2016/09/seatalk-arduino-interface-5v-ttl-for_19.html">Berreizeta</a>.

