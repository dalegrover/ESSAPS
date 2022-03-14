// ModBus
// modbus:  16-bit data:  high/low (big-endian)
//          16-bit crc:   low/high using CRC below
// for long discussion, see https://control.com/forums/threads/byte-order-of-modbus-crc.11616/


//    The function CRC16_2() is used under Creative Commons Attribution-ShareAlike 4.0 International License.
//
//    The remainder is
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



#include "DPS5015.h"

// address: default of DPS5015 is 0x0001
#define MODBUS_ADDRESS 0x01

// read register(s)
#define FUNCTION_READ 0x03

// write single register
#define FUNCTION_WRITE 0x06

// model, r
#define REG_MODEL 0x000b

// version, r
#define REG_VERSION 0x000c

#ifdef NO_DPS
unsigned int fakeRegs[50];
#endif


// set the modbus register, but verify it was set by reading.
// if okay, return 1, if not, return 0
unsigned int setModBusRegVerified(unsigned int reg, unsigned int val)
{
  unsigned int rv;

  rv=setModBusReg(reg,val);
  if(rv==0xffff)
  {
    Serial.print("Error writing ");
    Serial.print(val);
    Serial.print(" to register ");
    Serial.println(reg);
    return(0);
  }
  // now verify it took
  rv=getModBusReg(reg);
  if( rv == val)
  {
    return(1); // okay!
  }
  else
  {
    Serial.print("Error writing ");
    Serial.print(val);
    Serial.print(" to register ");
    Serial.print(reg);
    Serial.print(" actually read back ");
    Serial.println(rv);
    return(0);
  }
} // setModBusRegVerified()


// Simulate DPS?
#ifdef NO_DPS

unsigned int setModBusReg(unsigned int reg, unsigned int val)
{
  fakeRegs[reg]=val;
  return(val);  
}

#else

// sets modbus register to value
// returns 0xffff if error, else returns value returned by DSP5015
unsigned int setModBusReg(unsigned int reg, unsigned int val)
{
  //                     addr           write          regh regl valh vall crch crcl
  unsigned char buf[20] = { MODBUS_ADDRESS, FUNCTION_WRITE, 0xFF, 0xFF, 0xff, 0xff};
  int len;

  buf[2] = reg >> 8; // register high
  buf[3] = reg & 0xff; // register low
  buf[4] = val >> 8; // value high
  buf[5] = val & 0xff; // value low

  makeModBus(buf, 6); // len will be 8 with crc appended
  sendModBus(buf, 8); // send it
  len = getModBus(buf, 20); // get up to 20 chars back
  // but should get exactly 8 back
  if (len != 8)
  {
    printf("\n");
    for (int i = 0; i < len; i++)
    {
      printf("%02x ", buf[i]);
    }
    printf("\n");
    return (0xffff); // error
  }
  // addr, write, regh, regl, valh, vall, crch, crcl
  return ( (buf[5] << 8) + buf[6] );
} // setModBusReg()

#endif


// simulate DPS?
#ifdef NO_DPS

unsigned int getModBusReg(unsigned int reg)
{
  if( reg == REG_V_READ )
    return( readingIndex * 15 / 12 * 100);
  if( reg == REG_I_READ )
    return( fakeRegs[REG_I_SET] );
  return( fakeRegs[reg] );
}


#else

// returns 16-bit register value of reg or ffff for error
unsigned int getModBusReg(unsigned int reg)
{
  //                      addr read regh regl cnth cntl crch crcl
  unsigned char buf[20] = { MODBUS_ADDRESS, FUNCTION_READ, 0xFF, 0xFF, 0x00, 0x01};
  int len;

  buf[2] = reg >> 8; // register high
  buf[3] = reg & 0xff; // register low
  makeModBus(buf, 6); // len will be 8 with crc appended
  sendModBus(buf, 8); // send it
  len = getModBus(buf, 20); // get up to 20 chars back
  if (len != 7)
  {
    printf("\n");
    for (int i = 0; i < len; i++)
    {
      printf("%02x ", buf[i]);
    }
    printf("\n");
    return (0xffff); // error
  }
  // addr, read, bytesRead, regh, regl, crch, crcl
  return ( (buf[3] << 8) + buf[4] );
} // getModBusReg()

#endif


// sends out serial 2
int sendModBus(unsigned char *buf, int len)
{
  for (int i = 0; i < len; i++)
    Serial2.write(buf[i]);
} // sendModBus()


// get serial response until timeout
// or maxLen of buffer is met
// returns number of bytes read
int getModBus(unsigned char *buf, int maxLen)
{
  int i;
  int timeout;
  int done;

  timeout = 0; // in msecs
  done = false;
  while (!done)
  {
    if (Serial2.available())
      done = true;
    delay(1); // 1 msec
    timeout++;
    if (timeout > 500)
      return (0); // timed out waiting for initial transmission
  }

  i = 0; // count of bytes received
  timeout = 0;
  while (true)
  {
    if (i >= maxLen)
      return (i);

    if (Serial2.available())
    {
      buf[i] = Serial2.read();
      i++;
      timeout = 0; // reset timer
    }
    else
    {
      delay(1); // 1 msec
      timeout++;
      if (timeout > 10)
      {
        return (i); // timed out, last char received
      }
    }
  } // while(true)
  return (i); // should never get here...
}

// given the modbus data in a buffer that has room for 2 more chars,
// generate CRC and append.  returns length
int makeModBus(unsigned char *buf, int len)
{
  unsigned int crc, crcHigh, crcLow;
  crc = CRC16_2(buf, len);
  crcHigh = crc >> 8;
  crcLow = crc & 0xff;
  buf[len + 1] = crcHigh;
  buf[len] = crcLow;
  return (len + 2);
}



// from https://stackoverflow.com/questions/19347685/calculating-modbus-rtu-crc-16
// The following function is licensed under a Creative Commons Attribution-ShareAlike 4.0 International License.
unsigned int CRC16_2(unsigned char *buf, int len)
{
  unsigned int crc = 0xFFFF;
  for (int pos = 0; pos < len; pos++)
  {
    crc ^= (unsigned int)buf[pos];    // XOR byte into least sig. byte of crc

    for (int i = 8; i != 0; i--)
    { // Loop over each bit
      if ((crc & 0x0001) != 0)
      { // If the LSB is set
        crc >>= 1;                    // Shift right and XOR 0xA001
        crc ^= 0xA001;
      }
      else                            // Else LSB is not set
        crc >>= 1;                    // Just shift right
    } // for i
  } // for pos

  return crc;
}

// examples:
/*
    // go through registers
    printf("Registers\n");
    printf("V-set:  %0.2f\n", float(getModBusReg(REG_V_SET)) / 100.0 );
    printf("I-set:  %0.2f\n", float(getModBusReg(REG_I_SET)) / 100.0 );
    printf("V-read:  %0.2f\n", float(getModBusReg(REG_V_READ)) / 100.0 );
    printf("I-read:  %0.2f\n", float(getModBusReg(REG_I_READ)) / 100.0 );
    printf("On/Off:  %04x\n", getModBusReg(REG_ONOFF_SET) );
    printf("CVCC:  %04x\n", getModBusReg(REG_CVCS_READ) );

    delay(1000);
    setModBusReg(REG_ONOFF_SET, 0); // 0=off, disable output

    setModBusReg(REG_V_SET, 2300); // set 23.00V
    setModBusReg(REG_I_SET, 0005); // set 00.05A

    // go through registers
    printf("Registers\n");
    printf("V-set:  %0.2f\n", float(getModBusReg(REG_V_SET)) / 100.0 );
    printf("I-set:  %0.2f\n", float(getModBusReg(REG_I_SET)) / 100.0 );
    printf("V-read:  %0.2f\n", float(getModBusReg(REG_V_READ)) / 100.0 );
    printf("I-read:  %0.2f\n", float(getModBusReg(REG_I_READ)) / 100.0 );
    printf("On/Off:  %04x\n", getModBusReg(REG_ONOFF_SET) );
    printf("CVCC:  %04x\n", getModBusReg(REG_CVCS_READ) );

 */
