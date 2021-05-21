# SeaTalkNMEA

SeaTalk is a simple networking interface used by Raymarine to link their range of marine equipment 
together so that data can be shared across all devices. Technology wise it is best described by a
Thomas Knauf document found here:
<a href="http://www.thomasknauf.de/seatalk.htm">SeaTalk Technical Reference</a>.

This hardware project is designed to read SeaTalk data and convert it to standard 
<a href="https://en.wikipedia.org/wiki/NMEA_0183">NMEA 0183</a> data.
Such devices are available commercially but can be very expensive. 
The one manufactured by <a href="https://digitalyacht.co.uk/">Digital YACHT</a> is sold for £150 whereas
the solution here can built for £10 - £20.

The project is based around the Arduino Pro Micro. This device was chosen as it provided two
hardware serial ports. One port is accessed via the USB connection and the second
is available on the board. The intention being to read the SeaTalk data via the 
on board serial port and make the NMEA data available via the USB.
 
<img style="max-height:300px;" src="~/blob/main/Pictures/IMG_0611.JPG" />
 
SeaTalk is a serial interface that uses a 12 volt signal line. To interface this
to the Arduino it must be converted to TTL level, this can be done using the
circuit published by <a href="http://berreizeta.blogspot.com/2016/09/seatalk-arduino-interface-5v-ttl-for_19.html">Berreizeta</a>:

<img style="max-height:300px;" src="~/blob/main/Pictures/SeaTalk_12v_TTL.JPG" />
 
This can easily be built on Veroboard and the whole device assembled as:
 
<img style="max-height:300px;" src="~/blob/main/Pictures/IMG_0612.JPG" />
 
The Arduino is programmed via the USB connection and when connected to a SeaTalk 
device can produce sentences such as:
 
<pre>
 
 $PIMSST1,D,20,41,0,0*5e
 $GPVHW,,,,,0.0,N,0.0,K,*77
 $PIMSST1,D,0,42,64,0,0*41
 $GPDBT,0.0,f,0.0,M,0.0,F*06
 $GPDPT,0.0,0*49
 
</pre>
 
The NMEA sentences starting $P are proprietary sentences that show the actual SeaTalk data being processed. 
These are used for debugging purposes and can easily be turned off.

References:
    
- The only, and best, <a href="http://www.thomasknauf.de/seatalk.htm">SeaTalk Technical Reference</a> to be found on the internet.
- A 12v to TTL interface published by <a href="http://berreizeta.blogspot.com/2016/09/seatalk-arduino-interface-5v-ttl-for_19.html">Berreizeta</a>.

 

<img src="https://github.com/MartinDavidWaller/D70Box/blob/master/3DPrints/Pictures/D70KnobTopView.png?sanitize=true&raw=true" />