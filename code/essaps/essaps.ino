// ESSAPS
// Experimental Small-Scale Anodizing Power Supply
// 5.2
//
// Anodizing power supply and capacitor-discharge welder
// For ESP-WROOM-32 module connected to DPS5015 power supply
// w/ maximum of 50V 15A switching power supply
// Uses ModBus over TTL serial to talk to DPS5015
// TX2 is GPIO 12
// RX2 is GPIO 13
//
// Other I/O:
// GPIO 15:  piezo alarm, active low
// GPIO 2:  Green LED (anodizing active), active high
// GPIO 4:  Red LED (CDW ready to use), active high
// GPIO 13:  RX2 to TXD of DPS5015
// GPIO 12:  TX2 to RXD of DPS5015
// GPIO 14:  relay 1, active low
// GPIO 27:  relay 2, active low

// Relay 1:  hi (default, off) = +Anodize banana socket connected to DPS5015 output
//           lo (active low)   = +Anodize disconnected

// Relay 2:  hi (default, off) = CDW capacitor is discharged through 100 ohm 25W resistor
//           low (active low)  = CDW capacitor is connected to DPS5015 output

// Other connections
// VIN:  +5V in
// GND:  GND
// 3_3V:  Piezo VCC, Relay VCC (remove jumper JD-VCC to VCC, connect +5V to JD-VCC)

//
//  to do:
// if power supply not on, detect and note on web page
// add filtering to the maximum voltage and peak resistance calculation to remove any noise
// ability to save settings (incl. capacitor value)
// make delta v enable persistant, like buzzer
// remove CDW button unless idle/stop/etc.
// more space between buttons & checkboxes
// bold dynamic values


//    Copyright (C) 2021 Dale Grover
//
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <https://www.gnu.org/licenses/>.




#include <WiFi.h>
#include <WebServer.h>

#include "DPS5015.h"

// for debugging without connecting to an actual DPS unit,
// set NO_DPS in DPS5015.h

const char* progVersion = "5.3";

// SSID & Password
const char* ssid = "Anodizer5";  // Enter the SSID here for this device
const char* password = "987654321";  //Enter the password to log on to this device

// IP Address details
IPAddress local_ip(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

WebServer server(80);  // Object of WebServer(HTTP port, 80 is defult)

float anodizingAreaIn; // area in sq inches, all surfaces
float anodizingAreaFt; // anodizingAreaIn / 144, in sq ft
float anodizingCurrent; // 0.050A to 15A, float
float anodizingRampTime; // minutes
float anodizingMaxTime; // minutes
float anodizingPAR; // peak anodizing resistance (ohm sq ft)
float anodizingVPeak; // Volts, estimated peak voltage
float anodizingASF; // amp/sq ft
float anodizingThickness; // mils
float anodizingPeakRSF; // peak resistance per sq ft
float anodizingDeltaV; // change in V to signal end of cycle
int anodizingEnableDeltaV; // true or false to enable delta V end of cycle
float anodizingMaxV; // actual maximum voltage so far during CC

// CDW welding
float CDWCapacitance; // capacitance in Farads
float CDWEnergy; // energy per weld in Joules
float CDWVoltage; // voltage to give CDWEnergy given CDWCapacitance

// web page status string
char *statusString; // points to a status string--e.g., "Timed out"
char statusBuffer[50]; // in case you need a dynamic status--like "Delta V at 32.7 seconds"
char timeBuffer[20]; // for time, for above

int piezoEnable; // enable buzzer at end of anodizing cycle

// set max voltage to about 5 volts less than input voltage to DPS5015
float maxVoltage;

// second UART (to DPS5015 using modbus)
#define RXD2 13
#define TXD2 12

// piezo buzzer (active, active low) on GPIO15
#define PIEZO 15
#define PIEZO_OFF 1
#define PIEZO_ON 0

// LEDs
#define LED_ANODIZE 2
#define LED_CDW 4
#define LED_ON 1
#define LED_OFF 0

// relays
#define RELAY_1 14
#define RELAY_2 27
#define RELAY_ON 0
#define RELAY_OFF 1

#define BUFLEN 50


// States
#define STATE_IDLE 0
#define STATE_START 1
#define STATE_PAUSE 2
#define STATE_PAUSED 3
#define STATE_RAMP 4
#define STATE_CC 5
#define STATE_DONE 6
#define STATE_TIMED_OUT 7
#define STATE_STOP 8
#define STATE_STOPPED 9
#define STATE_DELTA_V 10
#define STATE_CDW_IDLE 11
#define STATE_TEST_CAP 12
#define STATE_CDW_STOP 13
#define STATE_CDW_CHARGE 14
#define STATE_CDW_CHARGING 15
#define STATE_CDW_READY 16
#define STATE_CDW_WAIT_TO_RESET 17


char *stateNames[] = {"IDLE", "START", "PAUSE", "PAUSED", "RAMP", "CONSTANT CURRENT", "DONE", \
                      "TIMED OUT", "STOP", "STOPPED", "DELTA-V HALT", "CDW IDLE", "TEST CAP", \
                      "CDW STOP", "CDW CHARGE", "CDW CHARGING", "CDW READY", "CDW WAIT TO RESET" \
                     };

int state;
int pauseState;

// data arrays, for 3 hours at 12 samples/minute (5 secs/sample)
#define ARRAY_HOURS 3
#define ARRAY_SAMPLES_PER_MINUTE 12
#define MAX_READINGS (ARRAY_HOURS * 60 * ARRAY_SAMPLES_PER_MINUTE)
float readingsCurrent[MAX_READINGS];
float readingsVoltage[MAX_READINGS];
int readingIndex;

void setup()
{
  // set up piezo
  pinMode(PIEZO, OUTPUT);
  digitalWrite(PIEZO, PIEZO_OFF);

  // set up LEDs
  pinMode(LED_ANODIZE, OUTPUT);
  pinMode(LED_CDW, OUTPUT);
  digitalWrite(LED_ANODIZE, LED_OFF);
  digitalWrite(LED_CDW, LED_OFF);

  // set up relays
  pinMode(RELAY_1, OUTPUT);
  pinMode(RELAY_2, OUTPUT);
  digitalWrite(RELAY_1, RELAY_ON); // disconnect anodize socket
  digitalWrite(RELAY_2, RELAY_OFF); // discharge CDW capacitor

  // serial via USB to host computer
  Serial.begin(115200);
  // serial to DSP5015 power supply (modbus)
  // factory default is 9600, can go to 19200
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  delay(4000); // let things settle (was 5000)

  // set to defaults, disable outputs
  setModBusRegVerified(REG_ONOFF_SET, 0); // 0=off, disable output
  // now calculate maximum output voltage, which is Vout = Vin/1.1
  maxVoltage = getModBusReg(REG_VIN_READ) / 100.0 / 1.1;
  setModBusRegVerified(REG_V_SET, int(maxVoltage * 100)); // set MAX_VOLTAGE * 100
  setModBusRegVerified(REG_I_SET, 5); // set 00.05A, minimal current

  // set up web server
  // Create SoftAP
  WiFi.disconnect(); // these 3 lines important:  https://github.com/espressif/arduino-esp32/issues/2025
  WiFi.mode(WIFI_OFF);
  delay(1000);
  WiFi.persistent(false);
  WiFi.softAP(ssid, password);
  delay(2000); // this delay is "very important"--bug in kernal?
  WiFi.softAPConfig(local_ip, gateway, subnet);

  Serial.print("\n\n----------------\nAnodizer v");
  Serial.print(progVersion);
  Serial.print("\n----------------\n");
  Serial.print("Access point: ");
  Serial.println(ssid);
  Serial.println(password);

  Serial.print("Maximum Voltage:  ");
  Serial.println(maxVoltage);

  // we only serve two things: the main page and the svg plot
  server.on("/", handle_root);
  server.on("/graph.svg", graphvi);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");

  state = STATE_IDLE;
  statusString = "Idle";

  // defaults
  anodizingAreaIn = 144.0; // area in inches (total, all surfaces/sides)
  anodizingASF = 4.5; // for LCD (low current density anodizing), 6 ASF and below
  anodizingThickness = 0.7; // 0.0007" = 0.7 mils thickness
  anodizingRampTime = 5; // default ramp time in minutes
  anodizingDeltaV = 0.05; // change in V to halt cycle (if enabled)
  anodizingEnableDeltaV = false;
  calculateParams();

  // CDW capacitor--this is default, but can test and set programmatically too
  CDWCapacitance = 0.091; // 91,000 uF
  CDWEnergy = 20.0; // 20J, default energy per weld
  calculateCDWVoltage();

  // Energy in CDW capacitor is 1/2 C V^2
  // For 45V and 90,000 uF (0.09F), E=91J.
  // (It so happens that at 45V, E in J is the C in MFD)

  piezoEnable = false; // no buzzer at end by default
  digitalWrite(PIEZO, PIEZO_OFF);

} // setup()


// given area, ASF, thickness, and ramp time, calculate
// current, time, est PAR, est VPeak

// This array maps ASF to PAR
// May need adjusting for sodium bifsulfate; may not be accurate.
// Some folks just use 0.95 ohm for everything, but I believe that may apply at high ASF
// 0 to 24 ASF
float ASFtoPAR[] = {5.00, 4.00, 3.00, 2.67, 2.50, 2.33, 2.17, 2.09, 2.00, 1.92, 1.84, 1.75, 1.67, 1.61, 1.55, 1.49, 1.43, 1.37, 1.31, 1.25, 1.19, 1.13, 1.07, 1.01, 0.95};

void calculateParams()
{
  // convert to sq feet
  anodizingAreaFt = anodizingAreaIn / 144.0;

  // find current
  anodizingCurrent = anodizingAreaFt * anodizingASF;

  // find approx peak anodizing resistance
  if ( anodizingASF > 23.0)
    anodizingASF = 23.0;
  anodizingPAR = ASFtoPAR[ (int) anodizingASF ];

  // find est. peak voltage
  anodizingVPeak = anodizingPAR * anodizingASF;

  // find time, in minutes ("720 rule")
  anodizingMaxTime = 720.0 * anodizingThickness / anodizingASF;
  // ramp time is on average 1/2 current, so add 1/2 ramp time
  anodizingMaxTime += anodizingRampTime / 2;
}


// given capacitance & energy, find voltage required
void calculateCDWVoltage()
{
  CDWVoltage = sqrt( CDWEnergy * 2 / CDWCapacitance);
  if (CDWVoltage > maxVoltage)
  {
    // requested energy requires too high voltage; drop down to maxVoltage
    CDWVoltage = maxVoltage;
    CDWEnergy = CDWCapacitance * CDWVoltage * CDWVoltage / 2.0;
  }
}

float startTime;  // in seconds (converted from millis)
int lastTime; // seconds, in int, last time through



// log readings to arrays
void logReadings(void)
{
  float voltage, current;
  float currentTime;

  // check if time for record sample
  currentTime = getSeconds();
  if ( ((int)currentTime == lastTime) || ( (int)currentTime % 5 != 0) )
  {
    lastTime = (int) currentTime;
    return;  // return if we haven't processed this
  }
  lastTime = (int) currentTime;

  // debug only
  Serial.println(readingIndex);

  voltage = (float)(getModBusReg(REG_V_READ)) / 100.0;
  current = (float)(getModBusReg(REG_I_READ)) / 100.0;

  readingsCurrent[readingIndex] = current;
  readingsVoltage[readingIndex] = voltage;

  readingIndex++;
  if (readingIndex >= MAX_READINGS)
    readingIndex = MAX_READINGS - 1;
  return;
}


// return mean value of last n points
float meanPoints(int n)
{
  int first, last, i;
  float sum = 0.0;
  first = readingIndex - n;
  if ( first < 1)
    return (0.0);
  if ( n == 0)
    return (0.0);
  for (i = 0; i < n; i++)
  {
    sum += readingsVoltage[i + first];
  }
  return ( sum / n);
}


float getSeconds(void)
{
  return ( millis() / 1000.0 );
}


// beep for one second
void beep()
{
  digitalWrite(PIEZO, PIEZO_ON);
  delay(1000);
  digitalWrite(PIEZO, PIEZO_OFF);
}


void loop()
{
  float currentTime; // in seconds (converted from millis)
  float current; // current reading
  float voltage;  // output voltage reading
  int setCurrent;

  server.handleClient();

  currentTime = getSeconds();

  switch (state)
  {
    case STATE_IDLE:
      // nothing to do
      // make sure LEDs and relays in default state
      digitalWrite(LED_ANODIZE, LED_OFF);
      digitalWrite(LED_CDW, LED_OFF);
      digitalWrite(RELAY_1, RELAY_ON); // disconnect anodize socket
      digitalWrite(RELAY_2, RELAY_OFF); // discharge CDW capacitor
      break;

    case STATE_START:
      int setc, setv;
      startTime = getSeconds(); // set start time for anodizing cycle
      // set the voltage to max, set current as given, turn on output
      setc = 0; // start current out at zero for ramp
      setv = int( maxVoltage * 100.0);
      setModBusRegVerified(REG_V_SET, setv); // voltage
      setModBusRegVerified(REG_I_SET, setc); // current
      setModBusRegVerified(REG_ONOFF_SET, 1); // 1=on, enable output
      anodizingPeakRSF = 0.0; // max resistance per sq ft, reset
      anodizingMaxV = 0.0; // max voltage, reset
      state = STATE_RAMP;
      statusString = "Ramping";
      // turn on anodizing LED
      digitalWrite(LED_ANODIZE, LED_ON);
      digitalWrite(RELAY_1, RELAY_OFF); // connect anodize socket
    // fall through to ramp state

    // ramping up
    case STATE_RAMP:
      logReadings();
      // calculate current for now
      currentTime = getSeconds(); // time since start in seconds
      if ( (currentTime - startTime) >= (anodizingRampTime * 60.0) )
      {
        // set to full current
        setCurrent = int( anodizingCurrent * 100.0);
        setModBusRegVerified(REG_I_SET, setCurrent);
        state = STATE_CC;
        statusString = "Constant Current Mode";
        break;
      }
      current = ((currentTime - startTime) / (anodizingRampTime * 60.0) ) * anodizingCurrent;
      // update power supply current
      setCurrent = int( current * 100.0);
      setModBusRegVerified(REG_I_SET, setCurrent);
      break;

    // constant current state--actively anodizing, hasn't reached peak or timed out
    case STATE_CC:
      logReadings();
      // time up?
      currentTime = getSeconds(); // time since start in seconds
      if ( (currentTime - startTime) >= anodizingMaxTime * 60)
      {
        setModBusRegVerified(REG_I_SET, 0);  // turn off current
        setModBusRegVerified(REG_ONOFF_SET, 0); // disable output
        state = STATE_TIMED_OUT;
        statusString = "Timed Out";
        // turn off LED
        digitalWrite(LED_ANODIZE, LED_OFF);
        if (piezoEnable)
          beep();
        break;
      }

      // read voltage, set if maximum
      voltage = (float)(getModBusReg(REG_V_READ)) / 100.0;
      if ( voltage > anodizingMaxV)
      {
        anodizingMaxV = voltage;
      }

      // check for peak
      //
      if (anodizingEnableDeltaV == true)
      {
        if ( voltage + anodizingDeltaV <= anodizingMaxV)
        {
          setModBusRegVerified(REG_I_SET, 0);  // turn off current
          setModBusRegVerified(REG_ONOFF_SET, 0); // disable output
          state = STATE_DELTA_V;
          // turn off LED
          digitalWrite(LED_ANODIZE, LED_OFF);
          timeString(timeBuffer, (int)currentTime);
          sprintf(statusBuffer, "Peak Resistance at %s", timeString);
          statusString = statusBuffer;
          if (piezoEnable)
            beep();
          break;
        }
      }
      break;

    case STATE_DELTA_V:
      // just hang out here
      break;

    case STATE_DONE:
      // just hang out here
      break;

    case STATE_TIMED_OUT:
      // just hang out here
      break;

    case STATE_PAUSE:
      setModBusRegVerified(REG_I_SET, 0);  // turn off current
      setModBusRegVerified(REG_ONOFF_SET, 0); // disable output
      pauseState = state; // capture the state pause is hit in
      state = STATE_PAUSED;
      if ( pauseState == STATE_RAMP)
        statusString = "Pause during ramp";
      else
        statusString = "Pause during constant current";
    // fall thru

    case STATE_PAUSED:
      // later on, add resume button
      // can check pauseState to find what state to return to
      break;

    case STATE_STOP:
      setModBusRegVerified(REG_I_SET, 0);  // turn off current
      setModBusRegVerified(REG_ONOFF_SET, 0); // disable output
      state = STATE_STOPPED;
      statusString = "Stop button pressed";
      // make sure LEDs and relays in default state
      digitalWrite(LED_ANODIZE, LED_OFF);
      digitalWrite(LED_CDW, LED_OFF);
      digitalWrite(RELAY_1, RELAY_ON); // disconnect anodize socket
      digitalWrite(RELAY_2, RELAY_OFF); // discharge CDW capacitor
    // fall thru

    case STATE_STOPPED:
      // just hang out here until a new cycle
      break;

    // tests capacitor value, sets to CDWCapacitance
    case STATE_TEST_CAP:
      {
        digitalWrite(RELAY_1, RELAY_ON); // disconnect anodize socket
        digitalWrite(RELAY_2, RELAY_ON); // connect CDW capacitor to power supply
        // turn off output
        setModBusRegVerified(REG_ONOFF_SET, 0); // disable output
        // set voltage to max output voltage
        setModBusRegVerified(REG_V_SET, int(maxVoltage * 100) ); // set voltage
        // set current to 0.1A
        setModBusRegVerified(REG_I_SET, 10);  // current to 00.10A
        // turn on output
        setModBusRegVerified(REG_ONOFF_SET, 1); // enable output
        // wait 5 seconds
        delay(5000);
        // get voltage
        float voltage;
        voltage = (float)(getModBusReg(REG_V_READ)) / 100.0;
        // turn off output
        setModBusRegVerified(REG_ONOFF_SET, 0); // disable output
        digitalWrite(RELAY_2, RELAY_OFF); // discharge CDW capacitor
        // calculate capacitance
        // C = 1/V * 0.1 * 5, in farads
        // C = 1/V * current * time

        CDWCapacitance = (1.0 / voltage) * 0.1 * 5.0;
      }
      state = STATE_CDW_IDLE;
      break;

    case STATE_CDW_IDLE:
      // CDW welding idle state
      digitalWrite(RELAY_1, RELAY_ON); // disconnect anodize socket
      digitalWrite(RELAY_2, RELAY_OFF); // discharge CDW capacitor
      digitalWrite(LED_CDW, LED_OFF);
      break;

    // start charging
    case STATE_CDW_CHARGE:
      Serial.print("CDW charge...");
      digitalWrite(RELAY_1, RELAY_ON); // disconnect anodize socket
      digitalWrite(RELAY_2, RELAY_ON); // CDW capacitor to power supply
      startTime = getSeconds(); // debug!!
      // turn off output
      setModBusRegVerified(REG_ONOFF_SET, 0); // disable output
      // set voltage
      setModBusRegVerified(REG_V_SET, (int)(CDWVoltage * 100));
      // set current to 1A--just an arbitrary rate, should be less than 5 seconds to full charge at 1F 50V
      setModBusRegVerified(REG_I_SET, 100);  // current to 01.00A
      // turn on output
      setModBusRegVerified(REG_ONOFF_SET, 1); // enable output
      Serial.print(" done in "); // debug
      Serial.println( getSeconds() - startTime);
      state = STATE_CDW_CHARGING;
      startTime = getSeconds(); // set start time for anodizing cycle
      break;

    case STATE_CDW_CHARGING:
      // have we been charging too long?
      currentTime = getSeconds(); // time since start in seconds
      if ( (currentTime - startTime) >= 15.0 ) // 15 seconds max
      {
        // should give some other error indication or suggestion to correct
        // this probably is a short circuit
        setModBusRegVerified(REG_ONOFF_SET, 0); // disable output
        state = STATE_CDW_IDLE;
        break;
      }
      // wait for voltage to get within 0.5V of set voltage
      voltage = (float)(getModBusReg(REG_V_READ)) / 100.0;
      if ( voltage + 0.5 >= CDWVoltage )
      {
        // switch over to 0.05A
        setModBusRegVerified(REG_I_SET, 5);  // current to 0.050A (the smallest supported)
        state = STATE_CDW_READY;
        digitalWrite(LED_CDW, LED_ON); // CDW ready LED
      }
      break;

    // ready, wait for short to bring to zero
    case STATE_CDW_READY:
      voltage = (float)(getModBusReg(REG_V_READ)) / 100.0;
      if ( voltage < 1.0 )
      {
        // set output voltage to 2V (still at 0.050A)
        setModBusRegVerified(REG_V_SET, 200);  // voltage to 02.00 V
        state = STATE_CDW_WAIT_TO_RESET;
        digitalWrite(LED_CDW, LED_OFF); // turn off CDW LED
      }
      break;

    // wait for voltage to go above 1V--i.e., open circuit, so can charge again
    case STATE_CDW_WAIT_TO_RESET:
      voltage = (float)(getModBusReg(REG_V_READ)) / 100.0;
      if ( voltage > 1.0 )
      {
        state = STATE_CDW_CHARGE;
      }
      break;

    case STATE_CDW_STOP:
      // turn off output
      setModBusRegVerified(REG_ONOFF_SET, 0); // disable output
      state = STATE_CDW_IDLE;
      digitalWrite(LED_CDW, LED_OFF);
      break;


    default:
      state = STATE_IDLE;



  } // switch(state)


} // loop()



// returns the argument index matching the name,
// or -1 if no match
int getArgWithName(char *name)
{
  for ( uint8_t i = 0; i < server.args(); i++)
  {
    if ( server.argName(i) == name)
      return (i);
  }
  return (-1);
}


// One URL does everything.
// First, any arguments are processed.  This could produce state changes.
// Then generate the HTML.  There are two modes--anodizing and CDW.
void handle_root()
{
  int dumpData = false;
  int rv = 0;

  // debug
  if ( server.args() > 1)
  {
    Serial.println("\nArguments:");
    for (uint8_t i = 0; i < server.args(); i++)
    {
      Serial.print(server.argName(i));
      Serial.print(":");
      Serial.println(server.arg(i));
    }
  }


  // calculate Button
  if ( getArgWithName("calcButton") != -1)
  {
    rv = getArgWithName("areaIn");
    if (rv != -1)
      anodizingAreaIn = server.arg(rv).toFloat();

    rv = getArgWithName("ASF");
    if (rv != -1)
      anodizingASF = server.arg(rv).toFloat();

    rv = getArgWithName("thickness");
    if (rv != -1)
      anodizingThickness = server.arg(rv).toFloat();

    rv = getArgWithName("rampTime");
    if (rv != -1)
      anodizingRampTime = server.arg(rv).toFloat();

    calculateParams();
  } // calcButton


  // Go Button

  if ( getArgWithName("goButton") != -1)
  {
    rv = getArgWithName("current");
    if (rv != -1)
      anodizingCurrent = server.arg(rv).toFloat();

    rv = getArgWithName("rampTime");
    if (rv != -1)
      anodizingRampTime = server.arg(rv).toFloat();

    rv = getArgWithName("maxTime");
    if (rv != -1)
      anodizingMaxTime = server.arg(rv).toFloat();

    rv = getArgWithName("enableDeltaV");
    if (rv != -1)
    {
      // only gets here if the value is checked
      anodizingEnableDeltaV = true;
      rv = getArgWithName("deltaV");
      if (rv != -1)
        anodizingDeltaV = server.arg(rv).toFloat();
    }
    else
      anodizingEnableDeltaV = false;

    rv = getArgWithName("enablePiezo");
    if (rv != -1)
    {
      piezoEnable = true;
    }
    else
      piezoEnable = false;

    state = STATE_START; // kick things off
    readingIndex = 0; // reset!

  } // goButton


  // process other actions buttons
  if ( getArgWithName("stopButton") != -1)
    state = STATE_STOP;

  if ( getArgWithName("pauseButton") != -1)
    state = STATE_PAUSE;

  if ( getArgWithName("dumpdataButton") != -1)
    dumpData = true;

  if ( getArgWithName("CDWButton") != -1)
    state = STATE_CDW_IDLE;

  if ( getArgWithName("capTestButton") != -1)
    state = STATE_TEST_CAP;


  if ( getArgWithName("anodizeButton") != -1)
    state = STATE_IDLE;

  if ( getArgWithName("calcVoltageButton") != -1)
  {
    rv = getArgWithName("energy");
    if (rv != -1)
      CDWEnergy = server.arg(rv).toFloat();
    calculateCDWVoltage();
  }

  if ( getArgWithName("CDWGoButton") != -1)
    state = STATE_CDW_CHARGE;

  if ( getArgWithName("CDWStopButton") != -1)
    state = STATE_CDW_STOP;


  // then, generate html response
  String html = ""; // output string, built up one section at a time
  char temp[1000]; // temp buffer for sprintf

  if ( state < STATE_CDW_IDLE )
  {


    // Build up sections of HTML
    html += "<!DOCTYPE html><html>";
    if ( (state == STATE_START) || (state == STATE_RAMP) ||  (state == STATE_CC) )
    {
      html += "<meta http-equiv='refresh' content='5; URL=http://192.168.1.1'/>"; // every 5 seconds
    }
    sprintf(temp,"<title>Anodizer v%s</title>", progVersion);
    html += temp;
    html += "<body>\
    <meta name=\"viewport\" content = \"width=device-width\">\
    <style>\
      body { \
      width: 320px;\
      font: 14pt Helvetica, sans-serif;\
      } \
      .btn { \
      font-size: 14pt; \
      } \
    </style>";
    sprintf(temp,"<h1>Anodizer v%s</h1>", progVersion);
    html += temp;

    html += "<h2>Anodizing Mode</h2>";

    // status line
    sprintf(temp, " State:  %s<br>", stateNames[state]);
    html += temp;

    sprintf(temp, " Status:  %s<br>", statusString);

    // if not running, allow input parameters
    if ( (state == STATE_IDLE) ||  (state == STATE_TIMED_OUT) ||  (state == STATE_DELTA_V) || (state == STATE_STOPPED))
    {
      // display part 1 data entry
      // area, ASF, thickness, ramp
      sprintf(temp, "<hr><form method=GET action=\"\">\
      Area <input type=text name=areaIn value=\"%0.2f\"> in^2<br>\
      ASF <input type=text name=ASF value=\"%0.2f\"> A/ft^2<br>\
      Thickness <input type=text name=thickness value=\"%0.1f\"> mils<br>\
      Ramp Time <input type=text name=rampTime value=\"%.1f\"> min<br>\
      <input class=\"btn\" type=submit name=calcButton value=\"CALCULATE BELOW\">\
      </form><hr>", anodizingAreaIn, anodizingASF, anodizingThickness, anodizingRampTime);
      html += temp;

      // display part 2, calculated data (but can over-ride)
      sprintf(temp, "<hr><form method=GET action=\"\">\
      Area %0.2f in^2 = %0.2f ft^2<br>\
      Current <input type=text name=current value=\"%0.2f\"> A<br>\
      Ramp Time <input type=text name=rampTime value=\"%0.1f\"> min<br>\
      Est. Total Time <input type=text name=maxTime value=\"%.1f\"> min<br>\
      Est. PAR %0.2f ohm*ft^2<br>\
      Est. V-peak %0.1f V / (PS Max %0.1f V)<br>\
      <input type=checkbox name=enableDeltaV value=unchecked> Delta V <input type=text name=deltaV value=\"%0.2f\"> V<br>\
      <input type=checkbox name=enablePiezo %s> Buzzer<br>\
      <input class=\"btn\" type=submit name=goButton value=\"GO\">\
      </form><hr>", anodizingAreaIn, anodizingAreaFt, anodizingCurrent, anodizingRampTime, \
              anodizingMaxTime, anodizingPAR, anodizingVPeak, maxVoltage, anodizingDeltaV, piezoEnable ? "checked" : "");
      html += temp;
    }

    // if active, display current voltage, current
    if ( (state == STATE_RAMP) ||  (state == STATE_CC) || (state == STATE_PAUSED))
    {
      float voltage, current; // read the actual values
      voltage = (float)(getModBusReg(REG_V_READ)) / 100.0;
      current = (float)(getModBusReg(REG_I_READ)) / 100.0;

      sprintf(temp, "%0.2f in^2; %0.2f ASF; %0.0f mils<br>", anodizingAreaIn, anodizingASF, anodizingThickness);
      html += temp;
      sprintf(temp, "Now: %0.1f V / Max so far: %0.1f V / Est peak: %0.1f V<br>", voltage, anodizingMaxV, anodizingVPeak);
      html += temp;
      sprintf(temp, "Now: %0.2f A / Target: %0.2f A<br>", current, anodizingCurrent);
      html += temp;
      float rsf;
      rsf = (voltage / current ) * anodizingAreaFt;
      if (state != STATE_RAMP)
      {
        if ( rsf > anodizingPeakRSF)
          anodizingPeakRSF = rsf;
      }
      sprintf(temp, "RSF: %0.2f / Max: %0.2f<br>", rsf, anodizingPeakRSF);
      html += temp;
      sprintf(temp, "PAR (est): %0.2f<br>", anodizingPAR);
      html += temp;
      if ( anodizingEnableDeltaV == true)
      {
        sprintf(temp, "Delta V enabled for Vmax-%0.2f<br>", anodizingDeltaV);
        html += temp;
      }
    }

    int timeNow = (int) (getSeconds() - startTime); // time since start in int seconds
    // ramping?
    if (state == STATE_RAMP)
    {
      char time0[10], time1[10];
      timeString(time0, timeNow); // int seconds
      timeString(time1, (int)(anodizingRampTime * 60));
      sprintf(temp, "Ramping:  %s / %s<br>", time0, time1);
      html += temp;
    }

    // if active, display time and max time
    if ( (state == STATE_RAMP) ||  (state == STATE_CC) || (state == STATE_PAUSED) || (state == STATE_TIMED_OUT) || (state == STATE_STOPPED) )
    {
      char time0[10], time1[10];
      timeString(time0, timeNow); // int seconds
      timeString(time1, (int)anodizingMaxTime * 60.0);
      sprintf(temp, "Time / Max:  %s / %s<br>", time0, time1);
      html += temp;
    }

    // add button to return to anodize state
    if ( (state == STATE_STOPPED) || (state==STATE_STOP) )
    {
      // anodize button
      html += "<form method=GET action=\"\">\
      <!-- <input type=hidden name=anodize value=\"none\">\ --> \
      <input class=\"btn\" type=submit name=anodizeButton value=\"ANODIZE MODE\">\
      </form>\<br>";
    }

    // add buttons for Stop and Pause
    html += "<form method=GET action=\"\">\
      <!-- <input type=hidden name=stop value=\"none\">\ --> \
      <input class=\"btn\" type=submit name=stopButton value=\"STOP\">\
      </form>\
      <form method=GET action=\"\">\
      <!-- <input type=hidden name=pause value=\"none\">\ --> \
      <input class=\"btn\" type=submit name=pauseButton value=\"PAUSE\">\
      </form><br>";

    // add button for CDW mode
    html += "<form method=GET action=\"\">\
      <!-- <input type=hidden name=CDW value=\"none\">\ --> \
      <input class=\"btn\" type=submit name=CDWButton value=\"CDW WELD\">\
      </form><br>";

    // add graph at bottom
    html += "<img src=\"/graph.svg\" />";
    html += "Blue=Current, Red=Voltage<br>";

    // add button for dump data
    html += "<form method=GET action=\"\">\
      <!-- <input type=hidden name=dumpdata value=\"dumpdata\">\ --> \
      <input class=\"btn\" type=submit name=dumpdataButton value=\"dumpdata\">\
      </form><br>";

    // dump data?
    if ( dumpData == true)
    {
      int i;
      for (i = 0; i < readingIndex; i++)
      {
        sprintf(temp, "%d, %0.02f, %0.02f<br>\n", i, readingsCurrent[i], readingsVoltage[i]);
        html += temp;
      }
    }

    // GPL
    html += "<br><br><hr>Copyright (C) 2021 Dale Grover<br> \
    This program is free software: you can redistribute it and/or modify \
    it under the terms of the GNU General Public License as published by \
    the Free Software Foundation, either version 3 of the License, or \
    (at your option) any later version. \
    <br> \
    This program is distributed in the hope that it will be useful, \
    but WITHOUT ANY WARRANTY; without even the implied warranty of \
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the \
    <a href =\"https://www.gnu.org/licenses/\"> GNU General Public License </a> for more details.";

    // close things up
    html += "</body></html>";

    // Serial.println(html);
  } // if anodizing modes

  else

    // CDW welding modes
  {
    html += "<!DOCTYPE html><html>";
    //if ( (state == STATE_TEST_CAP) || (state == STATE_CDW_CHARGE)|| (state == STATE_CDW_CHARGING) || (state == STATE_CDW_READY) || (state == STATE_CDW_WAIT_TO_RESET))
    if ( state == STATE_CDW_IDLE )
    {
      // nothing, else update every (1) second
    }
    else
    {
      html += "<meta http-equiv='refresh' content='1; URL=http://192.168.1.1'/>"; // update every second during cap test, other CDW modes
    }
    sprintf(temp,"<title>Anodizer v%s</title>", progVersion);     // html += "<title>Anodizer V5</title>"
    html += temp;
    html += "<body>\
    <meta name=\"viewport\" content = \"width=device-width\">\
    <style>\
      body { \
      width: 320px;\
      font: 14pt Helvetica, sans-serif;\
      } \
      .btn { \
      font-size: 14pt; \
      } \
    </style>";
    sprintf(temp,"<h1>Anodizer v%s</h1>", progVersion);
    html += temp;
    //<h1>Anodizer V5</h1>
    html += "<h2>CDW Mode</h2>";

    // status line
    sprintf(temp, " State:  %s<br>", stateNames[state]);
    html += temp;


    if ( state == STATE_TEST_CAP)
    {
      html += "Testing capacitor.  Will take a few seconds.";
    }
    else if ( state == STATE_CDW_IDLE)
    {
      sprintf(temp, "Capacitor:  %d uF<BR>", (int)(CDWCapacitance * 10000) * 100);
      html += temp;

      // cap test mode
      html += "<form method=GET action=\"\">\
      <!-- <input type=hidden name=capTest value=\"none\">\ --> \
      <input class=\"btn\" type=submit name=capTestButton value=\"CAP TEST\">\
      </form>\<br>";

      float maxEnergy;
      maxEnergy = CDWCapacitance * maxVoltage * maxVoltage / 2.0;

      sprintf(temp, "<hr><form method=GET action=\"\">\
      Energy <input type=text name=energy value=\"%0.2f\"> J (Max is %0.1f J)<br>\
      <input class=\"btn\" type=submit name=calcVoltageButton value=\"CALCULATE VOLTAGE\">\
      </form><hr>", CDWEnergy, maxEnergy);
      html += temp;

      sprintf(temp, "Voltage:  %.0f V<br>", CDWVoltage);
      html += temp;

      // CDW buttons
      html += "<form method=GET action=\"\">\
      <!-- <input type=hidden name=CDWGo value=\"none\">\ --> \
      <input class=\"btn\" type=submit name=CDWGoButton value=\"Weld\">\
      </form>\<br>";

      html += "<hr><br><br>";

      // anodize button
      html += "<form method=GET action=\"\">\
      <!-- <input type=hidden name=anodize value=\"none\">\ --> \
      <input class=\"btn\" type=submit name=anodizeButton value=\"ANODIZE MODE\">\
      </form>\<br>";
    }
    else if ( (state == STATE_CDW_CHARGE) || (state == STATE_CDW_CHARGING)  || (state == STATE_CDW_WAIT_TO_RESET)  || (state == STATE_CDW_READY))
    {
      sprintf(temp, "Energy:  %.0f J<br>", CDWEnergy);
      html += temp;
      sprintf(temp, "Target Voltage:  %.0f V<br>", CDWVoltage);
      html += temp;

      html += "<form method=GET action=\"\">\
      <!-- <input type=hidden name=CDWStop value=\"none\">\ --> \
      <input class=\"btn\" type=submit name=CDWStopButton value=\"STOP\">\
      </form>\<br>";

    }

    // GPL
    html += "<br><br><hr>Copyright (C) 2021 Dale Grover<br> \
    This program is free software: you can redistribute it and/or modify \
    it under the terms of the GNU General Public License as published by \
    the Free Software Foundation, either version 3 of the License, or \
    (at your option) any later version. \
    <br> \
    This program is distributed in the hope that it will be useful, \
    but WITHOUT ANY WARRANTY; without even the implied warranty of \
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the \
    <a href =\"https://www.gnu.org/licenses/\"> GNU General Public License </a> for more details.";

    // close things up
    html += "</body></html>";

  }
  server.send(200, "text/html", html);
} // handle_root()



// given a buffer (>10 chars long) and # of seconds,
// fills buffer with string "MMM:SS"
void timeString(char *p, int seconds)
{
  snprintf(p, 10, "%d:%02d", seconds / 60, seconds % 60);
  return;
}


// given float value v (0...maxv), scale so maxv->maxy
int scaley(float v, float maxv, int maxy)
{
  int y;
  y = maxy - (v / maxv) * (maxy - 10) - 5;
  //y = (int) (maxy - (( v / maxv * (maxy-10) )) + 5);
  return (y);
}

int scalex(int v, float maxv, float maxx)
{ int x;
  x = (float) v / (maxv - 1) * (maxx - 10) + 5;
  return (x);
}


// create graph of V & I
// V=red, I=green
//   readingsCurrent[readingIndex]=current;
//  readingsVoltage[readingIndex]=voltage;
// Note--could exceed the 65535 char limit of String;
// could just plot every Nth point for a total < 100 points, for example.
void graphvi()
{
  //int x1, x2, y1, y2;
  int x, xs, ys;
  String out = "";
  char temp[100]; // just enough for one line segment

  out += "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"320\" height=\"160\">\n";
  out += "<rect width=\"320\" height=\"160\" fill=\"rgb(255, 255, 255)\" stroke-width=\"1\" stroke=\"rgb(0, 0, 0)\" />\n";
  out += "<g stroke=\"blue\">\n";

  if (readingIndex >= 3)
  {
    // enough points to plot

    // CURRENT
    out += "<polyline points=\"";
    for (x = 0; x < readingIndex; x++)
    {
      xs = scalex(x, readingIndex, 320);
      ys = scaley(readingsCurrent[x], anodizingCurrent * 1.1, 160);
      sprintf(temp, "%d,%d ", xs, ys);
      out += temp;
    }
    out += "\" fill=\"none\" stroke=\"blue\" stroke-width=\"1\" />\n";

    // VOLTAGE
    out += "<polyline points=\"";
    for (x = 0; x < readingIndex; x++)
    {
      xs = scalex(x, readingIndex, 320);
      ys = scaley(readingsVoltage[x], maxVoltage * 1.1, 160);
      sprintf(temp, "%d,%d ", xs, ys);
      out += temp;
    }
    out += "\" fill=\"none\" stroke=\"red\" stroke-width=\"1\" />\n";


  } // if enough points to plot
  out += "</g>\n</svg>\n";
  server.send(200, "image/svg+xml", out);
}



void handleNotFound()
{

  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  server.send(404, "text/plain", message);
}
