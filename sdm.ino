/* esp8266-EnergyMeter
 * Copyright (c) 2021 David Froehlich
 * https://github.com/dfrli/esp8266-EnergyMeter
 */
#include <SoftwareSerial.h>

#define SDM_SER_BAUD 9600
#define SDM_SER_CONF SWSERIAL_8E1
#define SDM_REGISTER_COUNT 3 /* Number of SDM registers to read */
const uint16_t SDM_TIMEOUT /* Timeout [ms] for reading a SDM reply */
  = 100 + 9 * 22000 / SDM_SER_BAUD;
const uint32_t SDM_UPDATE_INTERVAL /* Interval [ms] between SDM readings */
  = 5 * 1000 * SDM_MAX_ID + SDM_TIMEOUT * SDM_REGISTER_COUNT;
const uint8_t MODBUS_T1_5 /* Modbus inter character time out */
  = (SDM_SER_BAUD > 19200 ?  750 : (16500+SDM_SER_BAUD-1)/SDM_SER_BAUD);
const uint8_t MODBUS_T3_5 /* Modbus frame delay */
  = (SDM_SER_BAUD > 19200 ? 1750 : (38500+SDM_SER_BAUD-1)/SDM_SER_BAUD);
SoftwareSerial swSerSDM(SDM_SWRX_PIN, SDM_SWTX_PIN);


union {
  char data[8];
  struct __attribute__((packed)) {
    uint8_t slaveAddress;
    uint8_t functionCode;
    uint8_t startAddress_hi;
    uint8_t startAddress_lo;
    uint8_t numberPoints_hi;
    uint8_t numberPoints_lo;
    uint8_t errorCheck_lo;
    uint8_t errorCheck_hi;
  };
} sdm_msgReq; /* Modbus Request */
union {
  char data[9];
  struct __attribute__((packed)) {
    uint8_t slaveAddress;
    uint8_t functionCode;
    uint8_t byteCount;
    uint8_t register1_hi;
    uint8_t register1_lo;
    uint8_t register2_hi;
    uint8_t register2_lo;
    uint8_t errorCheck_lo;
    uint8_t errorCheck_hi;
  };
} sdm_msgRes; /* Modbus Response */
#define SDM_MSG_WAIT -7 /* delay before ready */
#define SDM_MSG_IDLE -3 /* idle, ready for new requests */
#define SDM_MSG_PEND -1 /* pending, request sent, awaiting response */
int8_t sdm_msgPos = SDM_MSG_WAIT; /* State (<0) or Position (>=0) in SDM message */
uint32_t sdm_msgTime; /* Timestamp of message begin */

#define SDM_PHASE_1_POWER           0x000C
#define SDM_IMPORT_ACTIVE_ENERGY    0x0048
#define SDM_EXPORT_ACTIVE_ENERGY    0x004A
const uint16_t sdm_updateRegisters[SDM_REGISTER_COUNT] = /* Registers to poll */
  { SDM_PHASE_1_POWER, SDM_IMPORT_ACTIVE_ENERGY, SDM_EXPORT_ACTIVE_ENERGY };
uint8_t sdm_updateDevice; /* Node ID (>=1) */
uint8_t sdm_updateValue; /* updateRegister Index */

/* Calculate Modbus CRC */
uint16_t modbus_crc(const char *data, const uint8_t len) {
  uint16_t crc, flag;
  crc = 0xFFFF;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i];
    for (uint8_t j = 8; j > 0; j--) {
      flag = crc & 0x0001;
      crc >>= 1;
      if (flag)
        crc ^= 0xA001;
    }
  }
  return crc;
}

/* Determine update interval between two readings.
 * Interval will be increased after multiple consecutive read errors. */
inline uint32_t sdm_DevUpdateInterval(const uint8_t idx) __attribute__((always_inline));
uint32_t sdm_DevUpdateInterval(const uint8_t idx) {
  #ifdef DEBUG
    return SDM_UPDATE_INTERVAL + idx;
  #else
    if(idx < SDM_MAX_ID) return SDM_UPDATE_INTERVAL << ((sdm_errors[idx]&0x7F)>>4);
    else return SDM_UPDATE_INTERVAL;
  #endif
}

/* Calculate average over last n historical power meassurements (or seconds) */
int32_t sdm_PowerAverage(const uint8_t idx, uint16_t histCount) {
  if(idx < SDM_MAX_ID) {
    if(histCount > SDM_HIST_SIZE) {
      histCount = histCount / (SDM_UPDATE_INTERVAL/1000);
    }
    if(histCount > 1) {
      return historyAverage(histCount, sdm_powerHistPos[idx], SDM_HIST_SIZE, sdm_powerHist[idx]);
    } else {
      return sdm_powerHist[idx][sdm_powerHistPos[idx]];
    }
  }
  return 0;
}

/* Send Modbus request for a certain register to a node */
void sdm_SendRequest(const uint8_t node, const uint16_t addr) {
  if(sdm_msgPos != SDM_MSG_IDLE) {
    DEBUG_println(F("RS485 Not Idle"));
    return;
  }
  if(swSerSDM.available()) {
    DEBUG_println(F("RS485 Collision"));
    return;
  }
  if(!swSerSDM.availableForWrite()) {
    DEBUG_println(F("RS485 TX Buffer full"));
    return;
  }
  DEBUG_print(F("SDM #"));
  DEBUG_printf(node, DEC);
  DEBUG_print(F(" Request  ("));
  DEBUG_printHexWord(addr);
  DEBUG_println(')');
  sdm_msgPos = SDM_MSG_PEND;
  sdm_msgTime = currentMillis;
  memset(sdm_msgReq.data, 0, sizeof(sdm_msgReq.data));
  sdm_msgReq.slaveAddress = node;
  sdm_msgReq.functionCode = 0x04;
  sdm_msgReq.startAddress_hi = addr >> 8;
  sdm_msgReq.startAddress_lo = addr & 0xFF;
  sdm_msgReq.numberPoints_hi = 0;
  sdm_msgReq.numberPoints_lo = 2;
  const uint16_t crc = modbus_crc(sdm_msgReq.data, sizeof(sdm_msgReq.data) - 2);
  sdm_msgReq.errorCheck_lo = crc & 0xFF;
  sdm_msgReq.errorCheck_hi = crc >> 8;
  swSerSDM.flush();
  if(SDM_DERE_PIN != NOT_A_PIN) {
    digitalWrite(SDM_DERE_PIN, HIGH);
  }
  swSerSDM.write(sdm_msgReq.data, sizeof(sdm_msgReq.data));
  swSerSDM.flush();
  if(SDM_DERE_PIN != NOT_A_PIN) {
    digitalWrite(SDM_DERE_PIN, LOW);
  }
  if(node >= 1 && node <= SDM_MAX_ID) {
    const uint8_t idx = node - 1;
    if(sdm_errors[idx] < INT8_MAX) {
      sdm_errors[idx]++;
    }
  }
}

/* Parse a read Modbus message */
void sdm_Parse(void) {
  if(sdm_msgRes.slaveAddress < 1 || sdm_msgRes.slaveAddress > SDM_MAX_ID
  || sdm_msgRes.slaveAddress != sdm_msgReq.slaveAddress) {
    DEBUG_print(F("Invalid slaveAddress "));
    DEBUG_printlnf(sdm_msgRes.slaveAddress, DEC);
    return;
  }
  if(sdm_msgRes.functionCode != 4
  || sdm_msgRes.functionCode != sdm_msgReq.functionCode) {
    DEBUG_print(F("Invalid functionCode "));
    DEBUG_printlnf(sdm_msgRes.functionCode, DEC);
    return;
  }
  if(sdm_msgRes.byteCount != 4) {
    DEBUG_print(F("Invalid byteCount "));
    DEBUG_printlnf(sdm_msgRes.byteCount, DEC);
    return;
  }
  const uint16_t crc = modbus_crc(sdm_msgRes.data, sizeof(sdm_msgRes.data) - 2);
  if(crc != (sdm_msgRes.errorCheck_hi<<8 | sdm_msgRes.errorCheck_lo)) {
    DEBUG_print(F("Invalid CRC:"));
    for(uint8_t i = 0; i < sizeof(sdm_msgRes.data); i++) {
      DEBUG_print(' ');
      DEBUG_printHexByte(sdm_msgRes.data[i]);
    }
    DEBUG_print(' ');
    DEBUG_print('(');
    DEBUG_printHexWord(crc);
    DEBUG_println(')');
    return;
  }
  const uint8_t idx = sdm_msgRes.slaveAddress - 1;
  sdm_errors[idx] = 0;
  float val = NAN;
  ((uint8_t*)&val)[3] = sdm_msgRes.register1_hi;
  ((uint8_t*)&val)[2] = sdm_msgRes.register1_lo;
  ((uint8_t*)&val)[1] = sdm_msgRes.register2_hi;
  ((uint8_t*)&val)[0] = sdm_msgRes.register2_lo;
  DEBUG_print(F("SDM #"));
  DEBUG_printf(sdm_msgRes.slaveAddress, DEC);
  DEBUG_print(F(" Response (0x"));
  DEBUG_printHexByte(sdm_msgReq.startAddress_hi);
  DEBUG_printHexByte(sdm_msgReq.startAddress_lo);
  DEBUG_print(F("): "));
  DEBUG_println(val);
  const uint16_t regAddr = sdm_msgReq.startAddress_hi<<8 | sdm_msgReq.startAddress_lo;
  switch(regAddr) {
    case SDM_PHASE_1_POWER:
      sdm_power[idx] = val;
      sdm_powerHistPos[idx] = nextHistPos(sdm_powerHistPos[idx], SDM_HIST_SIZE);
      sdm_powerHist[idx][sdm_powerHistPos[idx]] = (int16_t)val;
      break;
    case SDM_IMPORT_ACTIVE_ENERGY:
      sdm_import[idx] = val;
      break;
    case SDM_EXPORT_ACTIVE_ENERGY:
      sdm_export[idx] = val;
      break;
  }
}

/* Process a received byte from RS485 Serial link */
void sdm_Process(const char sdmMsgByte) {
  if(sdm_msgPos >= 0 && sdm_msgPos < (int)sizeof(sdm_msgRes.data) - 1) {
    sdm_msgPos++;
  }
  else {
    sdm_msgPos = 0;
    sdm_msgTime = currentMillis;
    memset(sdm_msgRes.data, 0, sizeof(sdm_msgRes.data));
  }
  sdm_msgRes.data[sdm_msgPos] = sdmMsgByte;
  if(sdm_msgPos == sizeof(sdm_msgRes.data) - 1) {
    sdm_Parse(); /* Call Parse() method after all bytes received */
    sdm_msgPos = SDM_MSG_WAIT;
    sdm_msgTime = currentMillis;
  }
}



/* One-time setup */
void sdm_Setup(void) {
  if(SDM_DERE_PIN != NOT_A_PIN) {
    pinMode(SDM_DERE_PIN, OUTPUT);
    digitalWrite(SDM_DERE_PIN, LOW);
  }
  swSerSDM.begin(SDM_SER_BAUD, SDM_SER_CONF);
}

/* Periodic loop */
void sdm_Loop(void) {
  if(sdm_msgPos == SDM_MSG_WAIT) {
    if(millisDiff(sdm_msgTime) > MODBUS_T3_5 + 1) {
      sdm_msgPos = SDM_MSG_IDLE; /* Revert from WAIT to IDLE state after 3.5 character times */
    }
  }
  else if(sdm_msgPos > SDM_MSG_IDLE && millisDiff(sdm_msgTime) > SDM_TIMEOUT) {
    sdm_msgPos = SDM_MSG_WAIT; /* Go back to WAIT state after read timeout reached */
    sdm_msgTime = currentMillis;
  }
  if(swSerSDM.available()) {
    const char sdmMsgByte = swSerSDM.read();
    if(sdm_msgPos >= SDM_MSG_PEND) {
      sdm_Process(sdmMsgByte); /* Process received character if response message pending */
    } else {
      sdm_msgPos = SDM_MSG_WAIT; /* Remain in WAIT state when unexpected data in Serial link */
      sdm_msgTime = currentMillis;
    }
  }
  else if(sdm_msgPos == SDM_MSG_IDLE) {
    if(sdm_updateDevice > 0 && sdm_updateDevice <= SDM_MAX_ID) {
      if(sdm_updateValue < count(sdm_updateRegisters)) {
        sdm_SendRequest(sdm_updateDevice, sdm_updateRegisters[sdm_updateValue]);
      }
      if(sdm_updateValue < count(sdm_updateRegisters) - 1 && sdm_errors[sdm_updateDevice-1] <= 15) {
        sdm_updateValue++; /* Poll next value on next loop() cycle */
      } else {
        sdm_updateValue = 0; /* Finish polling after last value */
        sdm_updateDevice = 0;
      }
    } else {
      for(uint8_t i = 0; i < SDM_MAX_ID; i++) {
        if((sdm_timestamp[i] == 0 && millisOverflow == 0)
        || millisDiff(sdm_timestamp[i]) > sdm_DevUpdateInterval(i)) {
          sdm_timestamp[i] = currentMillis ? currentMillis : ~0;
          sdm_updateDevice = i + 1; /* Mark node to be polled on next loop() cycle */
          sdm_updateValue = 0; /* Start reading first value */
          break;
        }
      }
    }
  }
}
