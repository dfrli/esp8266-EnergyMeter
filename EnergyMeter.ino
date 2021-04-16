/* esp8266-EnergyMeter
 * Copyright (c) 2021 David Froehlich
 * https://github.com/dfrli/esp8266-EnergyMeter
 */
#include <ESP8266WiFi.h>

#define S0_PIN1        5 /* D1 */ /* S0(1) interrupt pin connected to (+) of S0 signal */
#define S0_PIN2        4 /* D2 */ /* S0(2) interrupt pin */
#define SML_PIN       14 /* D5 */ /* SML pin connected to NPN emitter of IR photo diode */
#define SDM_SWRX_PIN  13 /* D7 */ /* SDM RX pin connected to RS-485 MAX3485 (RO) */
#define SDM_SWTX_PIN  12 /* D6 */ /* SDM TX pin connected to RS-485 MAX3485 (DI) */
#define SDM_DERE_PIN  16 /* D0 */ /* SDM pin connected to MAX3485 DE and !RE */
#define WIFI_TXPOWER  10.0 /* dBm (range 17.5 .. 14.0 .. 10.0) */

const bool USE_S0  = true; /* Whether S0  support will be included */
const bool USE_SML = true; /* Whether SML support will be included */
const bool USE_SDM = true; /* Whether SDM support will be included */

/* WiFi Constants to connect to network without WiFiManager
** (uncomment to save into flash, comment to auto-connect to saved network) */
#ifndef WiFiManager_h
//#define WIFI_SSID "ESP8266"
//#define WIFI_PSK "12345678"
#endif

/* WiFi Constants for WiFiManager, SSID and PSK for Config Portal */
#ifdef WiFiManager_h
WiFiManager wifiManager;
static const char* const wifiSSID = "ESP8266";
static const char* const wifiPSK = "12345678";
#endif

//#define DEBUG
#undef DEBUG
#ifdef DEBUG
 #define DEBUG_print(x)        Serial.print(x);
 #define DEBUG_printf(x,y)     Serial.print(x,y);
 #define DEBUG_println(x)      Serial.println(x);
 #define DEBUG_printlnf(x,y)   Serial.println(x,y);
 #define DEBUG_printHexByte(x) Serial.print(x<0x10?'0':'\0');Serial.print(x,HEX);
 #define DEBUG_printHexWord(x) Serial.print(x<0x1000?'0':'\0');Serial.print(x<0x100?'0':'\0');\
                               Serial.print(x<0x10?'0':'\0');Serial.print(x,HEX);
#else
 #define DEBUG_print(x)
 #define DEBUG_printf(x,y)
 #define DEBUG_println(x)
 #define DEBUG_printlnf(x,y)
 #define DEBUG_printHexByte(x)
 #define DEBUG_printHexWord(x)
#endif

/* Count all elements in an array */
#define count(a) (sizeof(a)/sizeof(*a))

/* S0 Constants */
const uint8_t S0_CHANS = 2; /* Total number of S0 channels */
const uint8_t S0_HIST_EXP = 3; /* Save 2^n historical power values */
const uint16_t S0_HIST_SIZE = 1<<S0_HIST_EXP; /* Number of historical values */
const uint16_t S0_MIN_PULSE_LEN = 25; /* Minimum pulse length for debounce [ms] */
const uint16_t S0_MAX_POWER = 5000; /* Maximum acceptable power [W] for plausibility check */
const uint16_t S0_MIN_POWER = 2; /* Minimum acceptable power [W] for plausibility check */
const uint32_t S0_MAX_AGE = 3600*1000/S0_MIN_POWER; /* Maximum age of a power meassurement between two pulses */

/* S0 Globals */
uint32_t s0_pulse[S0_CHANS]; /* Number of pulses for meter constants > 1000 */
uint32_t s0_meterconstant[S0_CHANS] /* Meter Constant for each S0 channel in number of impulses per kWh */
 = { 1000u, 1000u };
uint32_t s0_bounce[S0_CHANS]; /* Timestamp of last bounce [ms] */
uint32_t s0_pulsespacing[S0_CHANS] /* Minimum time space between two pulses [ms] */
 = { 3600000000u / S0_MAX_POWER / s0_meterconstant[0], 3600000000u / S0_MAX_POWER / s0_meterconstant[1] };
uint32_t s0_energy[S0_CHANS]; /* Meter [Wh] */
int16_t  s0_power[S0_CHANS][S0_HIST_SIZE]; /* Ring buffer with historical power values [W] */
uint16_t s0_time[S0_CHANS][S0_HIST_SIZE]; /* Age of a historical record compared to previous one [s] */
uint16_t s0_histPos[S0_CHANS]; /* Current index in history ring buffer */

/* SDM Globals */
const uint8_t SDM_MAX_ID = 1; /* Highest Modbus ID (=maximum number) of Eastron SDM Meter */
const uint8_t SDM_HIST_EXP = 8-(SDM_MAX_ID<8?SDM_MAX_ID:8); /* Save 2^n historical power values */
const uint16_t SDM_HIST_SIZE = 1<<SDM_HIST_EXP; /* Number of historical values */
uint32_t sdm_timestamp[SDM_MAX_ID]; /* Timestamp of last update [ms] */
uint8_t  sdm_errors[SDM_MAX_ID]; /* Number of consecutive read errors or timeouts */
float    sdm_import[SDM_MAX_ID]; /* Import Meter [kWh] */
float    sdm_export[SDM_MAX_ID]; /* Export Meter [kWh] */
float    sdm_power[SDM_MAX_ID];  /* Current Power [W] */
int16_t  sdm_powerHist[SDM_MAX_ID][SDM_HIST_SIZE]; /* Ring buffer with historical power values [W] */
uint16_t sdm_powerHistPos[SDM_MAX_ID]; /* Current index in history ring buffer */

/* SML Globals */
const uint8_t SML_HIST_EXP = 7; /* Save 2^n historical power values */
const uint16_t SML_HIST_SIZE = 1<<SML_HIST_EXP; /* Number of historical values */
uint32_t sml_import; /* Import Meter [Wh] */
uint32_t sml_export; /* Export Meter [Wh] */
int32_t  sml_power;  /* Current Power [W] */
uint32_t sml_timestamp; /* Timestamp of last update [ms] */
int16_t  sml_powerHist[SML_HIST_SIZE]; /* Ring buffer with historical power values [W] */
uint16_t sml_powerHistPos; /* Current index in history ring buffer */

/* Div10 Functions for faster 10/100/1000 division by using bit shifting */
inline uint32_t div10(uint32_t num) __attribute__((always_inline));
uint32_t div10(uint32_t num) {
  uint32_t x, q;
  x = (num|1) - (num>>2); q = (x>>4) + x; x = q; q = (q>>8) + x; q = (q>>8) + x; q = (q>>8) + x; q = (q>>8) + x;
  return (q >> 3);
}
inline uint32_t div100(uint32_t num) __attribute__((always_inline));
uint32_t div100(uint32_t num) {
  uint32_t x, q;
  x = (num|1) - (num>>2); q = (x>>4) + x; x = q; q = (q>>8) + x; q = (q>>8) + x; q = (q>>8) + x; q = (q>>8) + x; num = (q >> 3);
  x = (num|1) - (num>>2); q = (x>>4) + x; x = q; q = (q>>8) + x; q = (q>>8) + x; q = (q>>8) + x; q = (q>>8) + x; num = (q >> 3);
  return num;
}
inline uint32_t div1000(uint32_t num) __attribute__((always_inline));
uint32_t div1000(uint32_t num) {
  uint32_t x, q;
  x = (num|1) - (num>>2); q = (x>>4) + x; x = q; q = (q>>8) + x; q = (q>>8) + x; q = (q>>8) + x; q = (q>>8) + x; num = (q >> 3);
  x = (num|1) - (num>>2); q = (x>>4) + x; x = q; q = (q>>8) + x; q = (q>>8) + x; q = (q>>8) + x; q = (q>>8) + x; num = (q >> 3);
  x = (num|1) - (num>>2); q = (x>>4) + x; x = q; q = (q>>8) + x; q = (q>>8) + x; q = (q>>8) + x; q = (q>>8) + x; num = (q >> 3);
  return num;
}
inline void divmod10(const uint32_t num, uint32_t &quotient, uint32_t &remainder) __attribute__((always_inline));
void divmod10(const uint32_t num, uint32_t &quotient, uint32_t &remainder) {
  uint32_t x, q;
  x = (num|1) - (num>>2); q = (x>>4) + x; x = q; q = (q>>8) + x; q = (q>>8) + x; q = (q>>8) + x; q = (q>>8) + x;
  quotient = (q >> 3);
  remainder = num - ((q & ~0x7) + (quotient << 1));
}

/* Time Accounting */
uint32_t currentMillis; /* Updated to millis() on every loop() cycle */
uint8_t millisOverflow; /* Number of millis() overflows */

/* Difference of ms between two millis() timestamps */
inline uint32_t millisDiff(const uint32_t current_millis, const uint32_t previous_millis) __attribute__((always_inline));
uint32_t millisDiff(const uint32_t current_millis, const uint32_t previous_millis) {
  return (current_millis < previous_millis)
    ? (UINT32_MAX - previous_millis + current_millis)
    : (current_millis - previous_millis);
}

/* Difference of ms between a millis() timestamp and current time */
inline uint32_t millisDiff(const uint32_t previous_millis) __attribute__((always_inline));
uint32_t millisDiff(const uint32_t previous_millis) {
  return millisDiff(currentMillis, previous_millis);
}

/* Uptime in seconds */
inline uint32_t uptime(void) __attribute__((always_inline));
uint32_t uptime(void) {
  return millisOverflow * 4294967 + div1000(currentMillis);
}

/* Next position within a ring buffer */
inline uint16_t nextHistPos(const uint16_t hPos, const uint16_t hSize) __attribute__((always_inline));
uint16_t nextHistPos(const uint16_t hPos, const uint16_t hSize) {
  return ((hPos) < ((hSize)-1) ? (hPos)+1 : 0);
}

/* Previous position within a ring buffer */
inline uint16_t prevHistPos(const uint16_t hPos, const uint16_t hSize) __attribute__((always_inline));
uint16_t prevHistPos(const uint16_t hPos, const uint16_t hSize) {
  return ((hPos) > 0 ? (hPos)-1 : (hSize)-1);
}

/* Calculate nearest 2^n to match a given target number, not exceeding a given maximum */
inline char nearestExponent(const int target, const int maximum) __attribute__((always_inline));
char nearestExponent(const int target, const int maximum) {
  char exponent = 0;
  while((2 << exponent) <= maximum && target > (((1 << (exponent)) + (1 << (exponent+1))) >> 1))
    exponent++;
  return exponent;
}

/* Calculate nearest 2^n to match a given target number */
inline char nearestExponent(const int target) __attribute__((always_inline));
char nearestExponent(const int target) {
  return nearestExponent(target, INT32_MAX/2);
}

/* Calculate average power value from a historical ring buffer for the last n power readings */
int32_t historyAverage(uint16_t histCount, uint16_t histPos, const uint16_t histArraySize, const int16_t histArray[]) {
  const uint8_t histExp = nearestExponent(histCount, histArraySize); /* estimate nearest 2^n for faster division */
  histCount = 1 << histExp;
  int32_t sum = 0;
  while(histCount > 0) {
    sum += histArray[histPos];
    histPos = prevHistPos(histPos, histArraySize);
    histCount--;
  }
  return sum >> histExp;
}
/* Calculate average power value from a historical ring buffer for the last n power readings
 * only considering values not older than a certain age [s] */
int32_t historyAverage(uint16_t histCount, uint16_t histPos, const uint16_t histArraySize, const int16_t histArray[], const uint16_t timeArray[], const uint16_t maxAge) {
  if(timeArray[histPos] == 0 && histArray[histPos] == 0) return 0; /* ring buffer value 0 is considered invalid/old */
  const uint8_t histExp = nearestExponent(histCount, histArraySize);
  histCount = 1 << histExp;
  int32_t sum = histArray[histPos];
  uint32_t age = timeArray[histPos];
  uint16_t num = 1;
  while(num < histCount) {
    histPos = prevHistPos(histPos, histArraySize);
    if(timeArray[histPos] == 0 && histArray[histPos] == 0) break;
    age += timeArray[histPos];
    if(age > maxAge) break;
    sum += histArray[histPos];
    num++;
  }
  if(num == 0) return 0;
  else if(num == 1) return sum;
  else if(num == histCount) return sum >> histExp;
  else return sum / num;
}

/* Whether S0 channel has a valid value to be displayed */
inline bool s0_hasValue(const uint8_t chan) __attribute__((always_inline));
bool s0_hasValue(const uint8_t chan) {
  return (USE_S0) && ( s0_energy[chan] != 0 );
}
/* Whether SDM device has a valid value to be displayed */
inline bool sdm_hasValue(const uint8_t idx) __attribute__((always_inline));
bool sdm_hasValue(const uint8_t idx) {
  return (USE_SDM) && ( sdm_timestamp[idx] != 0 );
}
/* Whether SML meter has a valid value to be displayed */
inline bool sml_hasValue(void) __attribute__((always_inline));
bool sml_hasValue(void) {
  return (USE_SML) && ( sml_timestamp != 0 );
}



void setup(void) {

  Serial.begin(115200);
  Serial.println(F("\n\n\n####\nStarting"));

  Serial.println(F("Initializing WiFi"));
  WiFi.mode(WIFI_STA); /* WiFi station (client) mode */
  WiFi.setPhyMode(WIFI_PHY_MODE_11G); /* Limit to 802.11g for more reliable connection */
  WiFi.setSleepMode(WIFI_NONE_SLEEP); /* Disable WiFi power saving for more reliable connection */
  WiFi.setOutputPower(WIFI_TXPOWER); /* Set WiFi TX Power as required */
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  wifi_country_t wifiCountry = { .cc = "DE", .schan = 1, .nchan = 13, .policy = WIFI_COUNTRY_POLICY_MANUAL };
  wifi_set_country(&wifiCountry); /* Set WiFi regulatory policy according to specified country */
  wifi_set_user_fixed_rate(FIXED_RATE_MASK_ALL, PHY_RATE_6); /* Limit to 802.11g 6 Mbps for more robust connection */
  wifi_set_user_sup_rate(RATE_11G6M, RATE_11G54M);
  wifi_set_user_rate_limit(RC_LIMIT_11G, 0, RATE_11G_G24M, RATE_11G_G6M);
  wifi_set_user_limit_rate_mask(LIMIT_RATE_MASK_ALL);
  WiFi.printDiag(Serial);

  #ifdef WiFiManager_h
    wifiManager.setConfigPortalTimeout(180);
    wifiManager.setConnectTimeout(60);
    if(!wifiManager.autoConnect(wifiSSID, wifiPSK)) {
      Serial.print(F("Failed to connect to WiFi and Config Mode reached timeout.\nRestarting...\n"));
      ESP.restart();
      delay(1000);
    }
  #else
    Serial.print(F("Connecting to WiFi "));
    #ifdef WIFI_SSID
      WiFi.begin(WIFI_SSID, WIFI_PSK);
    #else
      WiFi.begin();
    #endif
    Serial.print('"');
    Serial.print(WiFi.SSID());
    Serial.print('"');
    Serial.print(' ');
    while(WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print('.');
    }
    Serial.println('.');
  #endif

  if(WiFi.status() == WL_CONNECTED) {
    Serial.print(F("Connected to WiFi "));
    Serial.print('"');
    Serial.print(WiFi.SSID());
    Serial.print('"');
    Serial.print(' ');
    Serial.print('(');
    Serial.print(WiFi.BSSIDstr());
    Serial.print(')');
    Serial.print(F(" with IP Address "));
    Serial.print(WiFi.localIP());
    Serial.println();
  }

  Serial.print(F("Starting HTTP Server"));
  http_Setup();
  Serial.println('.');

  if(USE_S0) {
    Serial.print(F("Activating S0"));
    s0_Setup();
    Serial.println('.');
  }
  if(USE_SDM) {
    Serial.print(F("Activating SDM"));
    sdm_Setup();
    Serial.println('.');
  }
  if(USE_SML) {
    Serial.print(F("Activating SML"));
    sml_Setup();
    Serial.println('.');
  }
}



void loop(void) {
  if(millis() < currentMillis) {
    millisOverflow++;
  }
  currentMillis = millis();
  http_Loop();
  yield();
  if(USE_S0) {
    s0_Loop();
    yield();
  }
  if(USE_SDM) {
    sdm_Loop();
    yield();
  }
  if(USE_SML) {
    sml_Loop();
    yield();
  }
}
