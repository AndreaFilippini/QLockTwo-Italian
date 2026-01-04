# Functioning
The project is a clock that displays the time in words, using a large 11x10 matrix controlled by an Arduino microcontroller.
The time is extracted from an external RTC module.
After the time is then converted into words, a search for the words is performed in the matrix and then the corresponding LEDs are lit.
The matrix is driven by multiplexing, turning on the various LEDs individually at high speed using shift registers.

# Software dependencies
[Arduino IDE](https://www.arduino.cc/) for Arduino programming

# Components List
- **x 1** Arduino UNO
- **x 3** sn74hc595n shift registers
- **x 1** DS3231 RTC module
- **x 114** white LEDs (110 for the matrix, 4 for the minutes)
- **x 14** 330 ohm resistors

# Schematic
The connection diagram is linear and involves the use of 9 of the Arduino pins, from **5** to **13**, 3 for each shift register to control a total of 24 output pins (in this case, 21 outputs are sufficient for the LED matrix, since the matrix is made up of 10 rows and 11 columns).
The outputs of the registers are then connected to the inputs of the LED matrix.
There are also the connections to the RTC module and the four LEDs for the minutes.

<img src="https://github.com/AndreaFilippini/Qlocktwo-Italian/blob/main/Images/ArduinoSchematic.png"
     width="400"
     height="800">

# Final Result
<img src="https://github.com/AndreaFilippini/Qlocktwo-Italian/blob/main/Images/Result.jpg"
     width="400"
     height="800">

