#include <SoftwareSerial.h>

#define SML_SER_BAUD 9600
#define SML_SER_CONF SWSERIAL_8N1
const uint16_t SML_TIMEOUT = 100 + 512 * 10000 / SML_SER_BAUD; /* Timeout [ms] for reading one SML message */
SoftwareSerial swSerSML(SML_PIN, NOT_A_PIN);

union {
  int32_t value;
  struct __attribute__((packed)) {
    uint8_t b3;
    uint8_t b2;
    uint8_t b1;
  };
} sml_msgObis; /* received OBIS code */
const int32_t OBIS_IMP = 0x010800;
const int32_t OBIS_EXP = 0x020800;
const int32_t OBIS_PWR = 0x100700;

union {
  int64_t value;
  char data[8];
  int8_t int8;
  int16_t int16;
  int32_t int32;
  int64_t int64;
  uint8_t uint8;
  uint16_t uint16;
  uint32_t uint32;
  uint64_t uint64;
} sml_msgValue; /* received value */
int8_t sml_msgValuePos; /* byte position of value in SML message */
uint32_t sml_msgTime; /* Timestamp of message begin */

enum {
  SML_TYPE_UNKNOWN = 0,
  SML_TYPE_INT8   = 0x52,
  SML_TYPE_INT16  = 0x53,
  SML_TYPE_INT32  = 0x55,
  SML_TYPE_INT64  = 0x59,
  SML_TYPE_UINT8  = 0x62,
  SML_TYPE_UINT16 = 0x63,
  SML_TYPE_UINT32 = 0x65,
  SML_TYPE_UINT64 = 0x69
} __attribute__((packed)) sml_msgDataType; /* SML codes for data types */
#define sml_DataTypeLen(a) ((((char)a) & 0x0F) - 1)

enum {
  SML_INIT,     /* waiting for start sequence */
  RCVD_START,   /* received 0x1B */
  RCVD_START_2, /* received 0x1B 0x1B */
  RCVD_START_3, /* received 0x1B 0x1B 0x1B */
  RCVD_START_4, /* received 0x1B 0x1B 0x1B 0x1B */
  RCVD_START_5, /* received 0x1B 0x1B 0x1B 0x1B 0x01 */
  RCVD_START_6, /* received 0x1B 0x1B 0x1B 0x1B 0x01 0x01 */
  RCVD_START_7, /* received 0x1B 0x1B 0x1B 0x1B 0x01 0x01 0x01 */
  RCVD_START_8, /* received 0x1B 0x1B 0x1B 0x1B 0x01 0x01 0x01 0x01 */
  WAIT_ENTRY,   /* wait for entry */
  RCVD_ENTRY,   /* received 0x77 */
  RCVD_ENTRY_2, /* received 0x77 0x07 */
  RCVD_ENTRY_3, /* received 0x77 0x07 0x01 */
  RCVD_ENTRY_4, /* received 0x77 0x07 0x01 0x00 */
  READ_OBIS_1,  /* read OBIS byte 1 */
  READ_OBIS_2,  /* read OBIS byte 2 */
  READ_OBIS_3,  /* read OBIS byte 3 */
  RCVD_OBIS_3,  /* received OBIS byte 3 */
  RCVD_OBIS_FF, /* received OBIS end 0xFF */
  RCVD_OBIS_62, /* received 0x62 */
  RCVD_OBIS_52, /* received 0x52 */
  WAIT_INT,     /* wait for integer type */
  RCVD_INT,     /* received integer byte */
  RCVD_END,     /* received 0x1B */
  RCVD_END_2,   /* received 0x1B 0x1B */
  RCVD_END_3,   /* received 0x1B 0x1B 0x1B */
  RCVD_END_4    /* received 0x1B 0x1B 0x1B 0x1B */
  /* END received 0x1B 0x1B 0x1B 0x1B 0x1A */
} __attribute__((packed)) sml_msgState; /* State Machine */


/* Calculate average over last n historical power meassurements */
int32_t sml_PowerAverage(const uint16_t histCount) {
  if(histCount > 1) {
    return historyAverage(histCount, sml_powerHistPos, SML_HIST_SIZE, sml_powerHist);
  }
  return sml_power;
}

/* Parse value from a SML message */
void sml_Parse(void) {
  switch(sml_msgObis.value) {
    case OBIS_IMP:
      DEBUG_print(F("Import: "));
      DEBUG_println(sml_msgValue.uint32);
      sml_import = div10(sml_msgValue.uint32);
      break;
    case OBIS_EXP:
      DEBUG_print(F("Export: "));
      DEBUG_println(sml_msgValue.uint32);
      sml_export = div10(sml_msgValue.uint32);
      break;
    case OBIS_PWR:
      DEBUG_print(F("Power: "));
      DEBUG_println(sml_msgValue.int32);
      sml_power = sml_msgValue.int32;
      sml_timestamp = currentMillis ? currentMillis : ~0;
      sml_powerHistPos = nextHistPos(sml_powerHistPos, SML_HIST_SIZE);
      sml_powerHist[sml_powerHistPos] = (int16_t)sml_power;
      break;
  }
}

/* Process a received byte */
void sml_Process(const char smlMsgByte) {
  switch(sml_msgState) {
    case SML_INIT:
      if(smlMsgByte == 0x1B) {
        DEBUG_println(F("SML Message Start"));
        sml_msgTime = currentMillis;        
        sml_msgState = RCVD_START;
      }
      break;
    case RCVD_START:
      switch(smlMsgByte) {
        case 0x1B: sml_msgState = RCVD_START_2; break;
        default: sml_msgState = SML_INIT; break;
      }
      break;
    case RCVD_START_2:
      switch(smlMsgByte) {
        case 0x1B: sml_msgState = RCVD_START_3; break;
        default: sml_msgState = SML_INIT; break;
      }
      break;
    case RCVD_START_3:
      switch(smlMsgByte) {
        case 0x1B: sml_msgState = RCVD_START_4; break;
        default: sml_msgState = SML_INIT; break;
      }
      break;
    case RCVD_START_4:
      DEBUG_println(F("SML Header Received"));
      switch(smlMsgByte) {
        case 0x01: sml_msgState = RCVD_START_5; break;
        default: sml_msgState = SML_INIT; break;
      }
      break;
    case RCVD_START_5:
      switch(smlMsgByte) {
        case 0x01: sml_msgState = RCVD_START_6; break;
        default: sml_msgState = SML_INIT; break;
      }
      break;
    case RCVD_START_6:
      switch(smlMsgByte) {
        case 0x01: sml_msgState = RCVD_START_7; break;
        default: sml_msgState = SML_INIT; break;
      }
      break;
    case RCVD_START_7:
      switch(smlMsgByte) {
        case 0x01: sml_msgState = RCVD_START_8; break;
        default: sml_msgState = SML_INIT; break;
      }
      break;
    case RCVD_START_8:
      sml_msgState = WAIT_ENTRY;
      break;
    case WAIT_ENTRY:
      switch(smlMsgByte) {
        case 0x77: sml_msgState = RCVD_ENTRY; break;
        case 0x1B: sml_msgState = RCVD_END; break;
      }
      break;
    case RCVD_ENTRY:
      switch(smlMsgByte) {
        case 0x07: sml_msgState = RCVD_ENTRY_2; break;
        case 0x77: sml_msgState = RCVD_ENTRY; break;
        default: sml_msgState = WAIT_ENTRY; break;
      }
      break;
    case RCVD_ENTRY_2:
      switch(smlMsgByte) {
        case 0x01: sml_msgState = RCVD_ENTRY_3; break;
        case 0x77: sml_msgState = RCVD_ENTRY; break;
        default: sml_msgState = WAIT_ENTRY; break;
      }
      break;
    case RCVD_ENTRY_3:
      switch(smlMsgByte) {
        case 0x00: sml_msgState = RCVD_ENTRY_4; break;
        case 0x77: sml_msgState = RCVD_ENTRY; break;
        default: sml_msgState = WAIT_ENTRY; break;
      }
      break;
    case RCVD_ENTRY_4:
    case READ_OBIS_1:
      DEBUG_print(F("OBIS: "));
      DEBUG_printf(smlMsgByte, DEC);
      sml_msgObis.value = 0;
      sml_msgObis.b1 = smlMsgByte;
      sml_msgState = READ_OBIS_2;
      break;
    case READ_OBIS_2:
      DEBUG_print('.');
      DEBUG_printf(smlMsgByte, DEC);
      sml_msgObis.b2 = smlMsgByte;
      sml_msgState = READ_OBIS_3;
      break;
    case READ_OBIS_3:
      DEBUG_print('.');
      DEBUG_printlnf(smlMsgByte, DEC);
      sml_msgObis.b3 = smlMsgByte;
      sml_msgState = RCVD_OBIS_3;
      break;
    case RCVD_OBIS_3:
      sml_msgDataType = SML_TYPE_UNKNOWN;
      sml_msgValue.value = 0;
      sml_msgValuePos = INT8_MIN;
      switch(smlMsgByte) {
        case 0xFF: sml_msgState = RCVD_OBIS_FF; break;
        default: sml_msgState = WAIT_ENTRY; break;
      }
      break;
    case RCVD_OBIS_FF:
      switch(smlMsgByte) {
        case 0x62: sml_msgState = RCVD_OBIS_62; break;
        case 0x77: sml_msgState = RCVD_ENTRY; break;
      }
      break;
    case RCVD_OBIS_62:
      switch(smlMsgByte) {
        case 0x52: sml_msgState = RCVD_OBIS_52; break;
        case 0x77: sml_msgState = RCVD_ENTRY; break;
      }
      break;
    case RCVD_OBIS_52:
      sml_msgState = WAIT_INT;
      break;
    case WAIT_INT:
      switch(smlMsgByte) {
        case SML_TYPE_INT32: sml_msgDataType = SML_TYPE_INT32; break;
        case SML_TYPE_INT64: sml_msgDataType = SML_TYPE_INT64; break;
        case SML_TYPE_UINT32: sml_msgDataType = SML_TYPE_UINT32; break;
        case SML_TYPE_UINT64: sml_msgDataType = SML_TYPE_UINT64; break;
        case 0x77: sml_msgState = RCVD_ENTRY; break;
      }
      if(sml_msgDataType != SML_TYPE_UNKNOWN) {
        sml_msgState = RCVD_INT;
      }
      break;
    case RCVD_INT:
      if(sml_msgValuePos == INT8_MIN) {
        sml_msgValuePos = sml_DataTypeLen(sml_msgDataType) - 1;
      }
      if(sml_msgValuePos >= 0 && (unsigned int)sml_msgValuePos < sizeof(sml_msgValue.data)) {
        sml_msgValue.data[sml_msgValuePos] = smlMsgByte;
        sml_msgValuePos--;
      } else {
        if(sml_msgValuePos < 0 && smlMsgByte == 0x01) {
          DEBUG_print(F("Value: 0x"));
          DEBUG_printlnf(sml_msgValue.int32, HEX);
          sml_Parse(); /* Parse after value completely read */
        }
        sml_msgState = WAIT_ENTRY;
      }
      break;
    case RCVD_END:
      switch(smlMsgByte) {
        case 0x1B: sml_msgState = RCVD_END_2; break;
        case 0x77: sml_msgState = RCVD_ENTRY; break;
        default: sml_msgState = WAIT_ENTRY; break;
      }
      break;
    case RCVD_END_2:
      switch(smlMsgByte) {
        case 0x1B: sml_msgState = RCVD_END_3; break;
        case 0x77: sml_msgState = RCVD_ENTRY; break;
        default: sml_msgState = WAIT_ENTRY; break;
      }
      break;
    case RCVD_END_3:
      switch(smlMsgByte) {
        case 0x1B: sml_msgState = RCVD_END_4; break;
        case 0x77: sml_msgState = RCVD_ENTRY; break;
        default: sml_msgState = WAIT_ENTRY; break;
      }
      break;
    case RCVD_END_4:
      DEBUG_println(F("SML Message End"));
      switch(smlMsgByte) {
        case 0x1A: sml_msgState = SML_INIT; break;
        case 0x1B: sml_msgState = RCVD_END_4; break;
        case 0x77: sml_msgState = RCVD_ENTRY; break;
        default: sml_msgState = WAIT_ENTRY; break;
      }
      break;
  }
}

/* One-time setup */
void sml_Setup(void) {
  swSerSML.begin(SML_SER_BAUD, SML_SER_CONF);
  swSerSML.enableTx(false);
  swSerSML.enableRx(true);
}

/* Periodic loop */
void sml_Loop(void) {
  if(sml_msgState != SML_INIT && millisDiff(sml_msgTime) > SML_TIMEOUT) {
    sml_msgState = SML_INIT; /* Revert to INIT state after read timeout reached */
  }
  if(swSerSML.available()) {
    const char smlMsgByte = swSerSML.read();
    sml_Process(smlMsgByte);
  }
}
