// ModBus registers on DSP5015
//-----------------------------
// voltage set r/w, value is voltage*100
#define REG_V_SET 0x0000

// current set r/w, value is current*100
#define REG_I_SET 0x0001

// voltage read, r, value is voltage*100
#define REG_V_READ 0x0002

// current read, r, value is current*100
#define REG_I_READ 0x0003

// power read, r, value is power*10 or 100
#define REG_P_READ 0x0004

// voltage in, r, value is voltage*100
#define REG_VIN_READ 0x0005

// CVCS status, r, 0=CV, 1=CC
#define REG_CVCS_READ 0x0008

// on/off set, r/w, 0=Off 1=On
#define REG_ONOFF_SET 0x0009

// enable definition below to operate without an actual DPS5015
// power supply connected.
// #define NO_DPS
