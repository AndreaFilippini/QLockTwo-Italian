#include <Wire.h>
#include <RTClib.h>
#include <Arduino.h>

/*

  QlockTwo v.1.0
   ____  _            _    _                 
  / __ \| |          | |  | |                
 | |  | | | ___   ___| | _| |___      _____  
 | |  | | |/ _ \ / __| |/ / __\ \ /\ / / _ \ 
 | |__| | | (_) | (__|   <| |_ \ V  V / (_) |
  \___\_\_|\___/ \___|_|\_\\__| \_/\_/ \___/ 

*/

// Create RTC Object
RTC_DS3231 rtc;

// Set to 1 if you want to set the internal time of the RTC module during code upload to Arduino
#define setClockFlag    0

// Constants for defining the LED lighting speed during normal operation and testing
#define ledBrightness   1500
#define matrixTestVal   250

// Constants to define the maximum sentence size, the matrix size, the total number of words,
#define maxWordsChars   20
#define matrixCols      11
#define matrixRows      10
#define maxTotalWords   10

// Bit mask to isolate the part of the columns' turn-on bits (0x7FF -> 0b11111111111)
// Binary value with a number of 1 equal to the number of columns
#define matrixColsMask  0x7FF

// Constant to define the number of LEDs indicating the minutes remaining until the next time change
// The clock has an accuracy of 5 minutes; the intermediate minutes are indicated by lighting these LEDs
#define minutesLeftNum  4
#define minPrecision    5

// Constants to define the characteristics of the sn74hc595n registers for matrix multiplexing,
// including their number and number of outputs
#define registerNum     3
#define registerPins    3
#define registerOut     8
#define totalOutputPins (registerNum*registerOut)

// WORD TABLES --------------------------------------------------

// General structure of a word within the matrix, specifically, its row and column position and its length
struct Pos {
  int row;
  int col;
  int length;
};

// String array that mirrors the physical matrix
const char* matrixWords[] = {
    "SONORLEBORE",
    "ÈRLUNASDUEZ",
    "TREOTTONOVE",
    "DIECIUNDICI",
    "DODICISETTE",
    "QUATTROCSEI",
    "CINQUEAMENO",
    "ECUNOQUARTO",
    "VENTICINQUE",
    "DIECIPMEZZA"
};

// Words indicating each hour, from midnight to eleven
const char* hourWords[] = {
    "DODICI", "UNA", "DUE", "TRE", 
    "QUATTRO", "CINQUE", "SEI", "SETTE",
    "OTTO", "NOVE", "DIECI", "UNDICI"
};

// Words to indicate minutes, with intervals of five minutes
const char* minutesWords[] = {
    "MEZZA", "CINQUE", "DIECI", "QUARTO", "VENTI", "VENTICINQUE"
};

// PREFIXES -----------------------------------------------------
// Various prefixes to handle specific and general cases, including times after half past the hour and one o'clock
const char prefixGeneral[]     = "SONO LE ";
const char prefixForOne[]      = "È L ";
const char prefixBeforeHalf[]  = " E ";
const char prefixAfterHalf[]   = " MENO ";
const char prefixForFifteen[]  = " UN ";

// Pin for driving sn74hc595n registers
const int registers[registerNum][registerPins] = {
  //ST_CP, SH_CP, DS
  {11, 12, 13},
  {8, 9, 10},
  {5, 6, 7}
};

// Pin used to control the LEDs to indicate the intermediate times between one time and the next
const int minutesLedsLeft[minutesLeftNum] = {1, 2, 3, 4};

// Indexes and arrays to identify and store used and unused rows, in order to alternate between turning on used/unused rows
// and avoid the problem of ghosting during multiplexing for adjacent rows
// RowMapping array will contain the order in which to light up the rows in order to avoid this problem
// and will be refreshed every time the time changes, creating a new mapping.
int usedRowIndex, unusedRowIndex;
int usedRows[matrixRows];
int unusedRows[matrixRows];
int rowMapping[matrixRows];

// DATA -----------------------------------------------------
String words[maxWordsChars];
char matrixLeds[matrixRows][matrixCols];

// Definition of global variables that will be used during code execution, including hours and minutes,
// output to be sent to shift registers and the current time in words to be displayed on the matrix
String currentTime;
int hours, minutes, minutesLeft, prevHours, prevMinutes, totalWords, activeRow;
bool error = false;
uint32_t outputPinsValue;
byte singleRegisterValue;
Pos wordPos[maxTotalWords];

// --------------------------------------------------------------
// Convert time to textual string (Italian word clock style)
// --------------------------------------------------------------
String convertTimeToString(int hours, int minutes) {
  String out = "";

  // If minutes > 34 minutes, reference next hour, indicating the time as minutes remaining until the next hour
  if (minutes >= 35) {
    hours += 1;
  }

  // The hours after noon will be indicated starting from 12 noon again
  hours = hours % 12;

  // Special case of one o'clock with a different prefix
  if (hours == 1) {
    out += prefixForOne;
  } else {
    out += prefixGeneral;
  }

  // Concatenate the current time string with the corresponding hour word
  out += hourWords[hours];

  // If the time is “round”, don't add the minutes (0–4 min)
  if (minutes <= 4) {
    return out;
  }

  // Case to be handled for times before and after half past the hour
  if (minutes <= 34) {
    out += prefixBeforeHalf;
  } else {
    out += prefixAfterHalf;
  }

  // Special case of the quarter hour, with an additional prefix ("UN" before QUARTO at 15 or 45)
  if (((minutes >= 15) && (minutes <= 19)) ||
      ((minutes >= 45) && (minutes <= 49))) {
    out += prefixForFifteen;
  }

  // Get the word corresponding to the current minutes based on whether we are before or after the half hour
  int idx;
  if (minutes <= 34) {
    idx = (minutes % 30) / 5;
  } else {
    int temp = minutes - 35;
    idx = 5 - ((temp % 30) / 5);
  }

  // Add minutes to the current string 
  out += minutesWords[idx];

  return out;
}

// --------------------------------------------------------------
// Function to remove double spaces from the string indicating the current time
// --------------------------------------------------------------
String trimDoubleSpaces(String s) {
  while (s.indexOf("  ") != -1) {
    s.replace("  ", " ");
  }
  return s;
}

// --------------------------------------------------------------
// Function to split the string indicating the current time into a list of words
// --------------------------------------------------------------
int splitWords(String text, String result[]) {
  int count = 0;
  text.trim();
  int start = 0;

  // iterate until no other words are found
  while (true) {
	// Get the next space index char and if exists,
	// extrat the current word as substring and put into the string array result
    int spaceIndex = text.indexOf(' ', start);
    if (spaceIndex == -1) {
      result[count++] = text.substring(start);
      break;
    }
	// extract the word from the past start index and the next space char
    result[count++] = text.substring(start, spaceIndex);
    start = spaceIndex + 1;
    if (count >= maxWordsChars) break;
  }
  return count;
}

// --------------------------------------------------------------
// Function to find the word passed as a parameter within the matrix
// --------------------------------------------------------------
Pos searchWordInMatrix(String text, Pos previousPos) {
  Pos p;
  p.row = -1;
  p.col = -1;
  p.length = -1;

  // Initialize the variables with the position of the previous word,
  // the next target word in the matrix must follows the previous one
  int startRow = previousPos.row;
  int startCol = previousPos.col + previousPos.length;

  // Iterate across all matrix rows to find the target word
  for(int i = 0; i < matrixRows; i++){
      int index = String(matrixWords[i]).indexOf(text);
	  
	  // If the word was found in the current row and is in a subsequent row
	  // or is in the same row as the previous one but in a subsequent column,
	  // then return the current word and set the corresponding LEDs as HIGH
      if((index != -1) && ((i > startRow) || ((i == startRow) && (index >= startCol)))){
          p.row = i;
          p.col = index;
          p.length = text.length();
          setWordInLedMatrix(p, text);
          return p;
      }
  }
  return p;
}

// --------------------------------------------------------------
// Function to search for all words in the matrix and set the corresponding LEDs to on
// --------------------------------------------------------------
void searchAllWordsInMatrix(int totalWordsCount, String totalWords[], Pos wordPositions[]){
  // Initialize the position of the previous word with default values
  Pos previousPos;
  previousPos.row = 0;
  previousPos.col = 0;
  previousPos.length = 0;

  // Iterate over all words that make up the current time string
  for(int i = 0; i < totalWordsCount; i++){
    wordPositions[i] = searchWordInMatrix(totalWords[i], previousPos);
    previousPos = wordPositions[i];
  }
}

// --------------------------------------------------------------
// Reset the status of all matrix LEDs to LOW
// --------------------------------------------------------------
void setupLedMatrix() {
  for (int y = 0; y < matrixRows; y++) {
    for (int x = 0; x < matrixCols; x++) {
      matrixLeds[y][x] = LOW;
    }
  }
}

// --------------------------------------------------------------
// Function used to set the set the corresponding LEDs of a word to HIGH
// --------------------------------------------------------------
void setWordInLedMatrix(Pos p, String word) {
  int row = p.row;
  int col = p.col;
  for (int i = 0; i < word.length(); i++) {
    matrixLeds[row][col + i] = HIGH;
  }
}

// --------------------------------------------------------------
// Test function to display a stylized matrix showing the current status of the LEDs on the serial monitor
// --------------------------------------------------------------
void printMatrix(){
  char rowWords[matrixCols+1];

  // Iterate over each LED in the matrix and if its status is HIGH,
  // display it with the character ‘O’, otherwise display a dash
  for (int y = 0; y < matrixRows; y++) {
    for (int x = 0; x < matrixCols; x++) {
      if(matrixLeds[y][x] == HIGH){
        rowWords[x] = 'O';
      }else{
        rowWords[x] = '-';
      }
    }
    rowWords[matrixCols] = '\0';
    Serial.println(rowWords);
  }
}

// --------------------------------------------------------------
// Function to control shift registers
// --------------------------------------------------------------
void setRegistersOutput(uint32_t outputValue){

  // Iterate over all possible output pins, stepping through each iteration of
  // the maximum number of outputs for each shift register
  for (int i = 0; i < totalOutputPins; i+= registerOut) {
  
      // Get the index of the current shift register and the corresponding pins to drive it
      int registerIndex = i / registerOut;
      int latchPin = registers[registerIndex][0];
      int clockPin = registers[registerIndex][1];
      int dataPin = registers[registerIndex][2];
 
      // Get the 8 bits corresponding to the 8 outputs of the current register from the 32-bit input number
      singleRegisterValue = (outputValue >> i) & 0xFF;

      // Send the 8 bits to the register and set its outputs
      digitalWrite(latchPin, LOW);
      shiftOut(dataPin, clockPin, MSBFIRST, singleRegisterValue);
      digitalWrite(latchPin, HIGH);
  }
}

// --------------------------------------------------------------
// Function to determine whether a row of the matrix has at least one LED on
// --------------------------------------------------------------
bool isRowUsed(int rowIndex){

  // Iterate over the current line and if one of the LEDs is on, return true, otherwise false
  for(int col = 0; col < matrixCols; col++){
    if(matrixLeds[rowIndex][col] == HIGH){
      return true;
    }
  }
  return false;
}

// --------------------------------------------------------------
// Function to determine the order in which rows are lit in order to avoid ghosting
// --------------------------------------------------------------
void createRowMapping(){
  usedRowIndex = 0;
  unusedRowIndex = 0;

  // Split unused rows from those for which at least one LED is lit
  for(int row = 0; row < matrixRows; row++){
    if(isRowUsed(row)){
      usedRows[usedRowIndex] = row;
      usedRowIndex++;
    }else{
      unusedRows[unusedRowIndex] = row;
      unusedRowIndex++;
    }
  }

  // The mapping is created by alternating the lighting of a used row with an unused one
  for(int row = 0; row < usedRowIndex; row++){
    rowMapping[(row << 1)] = usedRows[row];
    rowMapping[(row << 1)+1] = unusedRows[row];
  }
}

// --------------------------------------------------------------
// Function to turn on the matrix based on the status of individual LEDs
// The function implements matrix multiplexing by iteratively turning on one row at a time
// --------------------------------------------------------------
void refreshMatrix() {
  // The default matrix state msut be all columns to HIGH, while rows are set to LOW
  // When you want to turn on an LED, switch the row state to HIGH and the corresponding column to LOW
  // The logic of the columns is inverted, so the bits of the columns must be negated afterwards
  // For example, if you want to turn on the LED at position [0, 1], first row and second coloumn, the steps will be as follows:
  // 24 bits corresponding to all register outputs, 8 for each: 0b[---][0000000000][00000000000] (first 3 bits are unused)
  // Turn the second LSB of the row to 1, so 0b[---][0000000001][00000000000] and then
  // turn the first LSB of the coloumns to 1, so 0b[---][0000000001][00000000010]
  // finally, negate the coloumns bits to get the final value, like 0b[---][0000000001][11111111101]
  
  // Itera on all rows used in rowMapping
  for(int row = (usedRowIndex << 1); row >= 0; row--){
    // Get the corresponding row based on the mapping created previously
    activeRow = rowMapping[row];
	
	// Set the current output value to default value
    outputPinsValue = 0;

	// Iterate over each column of the current row to construct the value to be sent to the shift registers
    for(int col = 0; col < matrixCols; col++){
	
	  // If the LED at the intersection of the current row and column is on,
	  // then set the corresponding bit in the output value to 1
      if(matrixLeds[activeRow][col] == HIGH){
	    // The first [11 bits] least significant bits drive the columns, while the most significant [10 bits] drive the rows
        outputPinsValue |= (1UL << (matrixCols + activeRow)) | (1UL << col);
      }
    }	
	// Negate the bits that drive the matrix columns and send the final value to the shift registers
	// the matrixColsMask is used to isolte the LSB of the coloumn bits
    outputPinsValue = ((outputPinsValue >> matrixCols) << matrixCols) | (matrixColsMask & ~outputPinsValue);
    setRegistersOutput(outputPinsValue);
    delayMicroseconds(ledBrightness);
  }
}

// --------------------------------------------------------------
// Test function to sequentially turn on all LEDs in the matrix
// --------------------------------------------------------------
void testMatrix(){
  for(int row = 0; row < matrixRows; row++){
    for(int col = 0; col < matrixCols; col++){
      outputPinsValue = 0;
      outputPinsValue |= (1UL << (matrixCols + row)) | (1UL << col);
      outputPinsValue = ((outputPinsValue >> matrixCols) << matrixCols) | (matrixColsMask & ~outputPinsValue);

      setRegistersOutput(outputPinsValue);
      delay(matrixTestVal);
    }
  }
}

// --------------------------------------------------------------
// Function to turn on LEDs that indicate intermediate minutes
// --------------------------------------------------------------
void refreshMinutesLeds(int minutesLeft){
  for(int led = 0; led < minutesLeftNum; led++){
    if(led < minutesLeft){
      digitalWrite(minutesLedsLeft[led], HIGH);
    }else{
      digitalWrite(minutesLedsLeft[led], LOW);
    }
  }
}

// --------------------------------------------------------------
// Setup arduino standard function 
// --------------------------------------------------------------
void setup() {
  // Initialize serial communication
  Serial.begin(9600);
  
  // Set the variables containing the previous time before the change with default values.
  prevHours = -1;
  prevMinutes = -1;

  // Set all pins that drive the shift registers as outputs
  for(int i = 0; i < registerNum; i++){
    for(int pin = 0; pin < registerPins; pin++){
      pinMode(registers[i][pin], OUTPUT);
    }
  }

  // If the RTC module was not found, then set the error variable to true
  if (!rtc.begin()) {
    error = true;
    Serial.println("Couldn't find RTC");
  }

  // If the flag to set the RTC module to the current time is true,
  // then call the adjust function on the rtc object
  if(setClockFlag){
    DateTime compiledTime(F(__DATE__), F(__TIME__));
    rtc.adjust(compiledTime);
    Serial.print("RTC time set as: ");
    Serial.print(compiledTime.hour());
    Serial.print(":");
    if (compiledTime.minute() < 10) Serial.print('0');
    Serial.println(compiledTime.minute());
  }
}

// --------------------------------------------------------------
// Loop arduino standard function 
// --------------------------------------------------------------
void loop() {

  // If the RTC module was not found, then execute a default function
  if(error){
    testMatrix();
    return;
  }

  // Get the time from the RTC module and save the hours and minutes in the corresponding variables
  DateTime now = rtc.now();
  hours = now.hour();
  minutes = now.minute();

  // Static assignments in hours and minutes variables for testing
  //hours = 18;
  //minutes = 55;

  // If there has been a change in hours or minutes
  if((prevHours != hours) || (prevMinutes != minutes)){
    
	// If there has been a change in minutes such that the sentence on the matrix needs to be changed
    if((prevMinutes / minPrecision) != (minutes / minPrecision)){
	
		// Convert the current time into a sentence, reset the matrix, split the string into several words,
		// set the corresponding LEDs for the words to HIGH status and finally create the mapping for matrix rows
        String currentTime = trimDoubleSpaces(convertTimeToString(hours, minutes));
        setupLedMatrix();
        totalWords = splitWords(currentTime, words);
        searchAllWordsInMatrix(totalWords, words, wordPos);
        createRowMapping();
    }else{
	      // Otherwise, update the LEDs indicating the minutes between intermediate times
          minutesLeft = minutes % 5;
          refreshMinutesLeds(minutesLeft);
    }
	
	// Update the variables containing the previous time with the current time
    prevHours = hours;
    prevMinutes = minutes;
  }

  // Turn on the LEDs with HIGH status with multiplexing
  refreshMatrix();
}