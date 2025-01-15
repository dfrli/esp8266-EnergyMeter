/* esp8266-EnergyMeter
 * Copyright (c) 2021 David Froehlich
 * https://github.com/dfrli/esp8266-EnergyMeter
 */

/* Reset S0 Meter Values */
void s0_ClearCounter(const uint8_t chan) {
  if(chan < S0_CHANS) {
    DEBUG_print(F("Clear channel="));
    DEBUG_print(chan + 1);
    DEBUG_print('\n');
    s0_DisableInterrupt(chan);
    s0_pulse[chan] = 0;
    s0_energy[chan] = 1;
    s0_histPos[chan] = 0;
    memset(s0_power[chan], 0, sizeof(s0_power[0]));
    memset(s0_time[chan], 0, sizeof(s0_time[0]));
    s0_EnableInterrupt(chan);
  }
}

/* Calculate age of last reading */
uint16_t s0_Age(const uint8_t chan) {
  if(chan < S0_CHANS) {
    uint16_t age = millisDiff(s0_bounce[chan]) >> 10;
    if(age == 0) age = 1;
    s0_time[chan][s0_histPos[chan]] = age;
    return age;
  }
  return 0;
}

/* Calculate average over last n historical power meassurements (or seconds) */
int32_t s0_PowerAverage(const uint8_t chan, const uint16_t histCount) {
  if(chan < S0_CHANS) {
    if(histCount > S0_HIST_SIZE) { /* "Large" number meaning average over last n seconds */
      s0_Age(chan);
      return historyAverage(S0_HIST_SIZE, s0_histPos[chan], S0_HIST_SIZE, s0_power[chan], s0_time[chan], histCount);
    }
    else if(histCount > 1) { /* "Small" number meaning average over last n meassurements */
      s0_Age(chan);
      return historyAverage(histCount, s0_histPos[chan], S0_HIST_SIZE, s0_power[chan], s0_time[chan], S0_MAX_AGE / 1000);
    } else {
      return s0_power[chan][s0_histPos[chan]];
    }
  }
  return 0;
}

/* Validate S0 latest power value and invalidate those older than maxAge */
void s0_ValidatePower(void) {
  uint16_t histPos;
  for(uint8_t i = 0; i < S0_CHANS; i++) {
    histPos = s0_histPos[i];
    if((s0_time[i][histPos] != 0 || s0_power[i][histPos] != 0)
    && millisDiff(s0_bounce[i]) > S0_MAX_AGE) {
      DEBUG_print(F("Invalidating S0 Power #"));
      DEBUG_println(i);
      s0_time[i][histPos] = S0_MAX_AGE / 1000 + 1;
      histPos = nextHistPos(histPos, S0_HIST_SIZE);
      s0_power[i][histPos] = 0;
      s0_time[i][histPos] = 0;
      s0_histPos[i] = histPos;
    }
  }
}

/* Routine triggered on every signal impulse */
void IRAM_ATTR interruptRoutine(const uint8_t chan) {
  if(chan < S0_CHANS) {
    const uint32_t t = millisDiff(s0_bounce[chan]); /* Time between two pulse */
    if(t < S0_MIN_PULSE_LEN || t < s0_pulsespacing[chan]) {
      s0_bounce[chan] = currentMillis;
      return; /* Discard to fast bounced signals */
    }
    s0_bounce[chan] = currentMillis;
    if(s0_meterconstant[chan] <= 1000) {
      s0_energy[chan] += 1000 / s0_meterconstant[chan];
    } else if(++s0_pulse[chan] >= div1000(s0_meterconstant[chan])) {
      s0_energy[chan] += 1;
      s0_pulse[chan] = 0;
    } else {
      return; /* Do nothing if no new energy meter value calculated */
    }
    const uint16_t oldHistPos = s0_histPos[chan];
    const uint16_t newHistPos = nextHistPos(oldHistPos, S0_HIST_SIZE);
    const uint32_t p = ((1000 * 1000 / s0_meterconstant[chan] * 3600) + (t - 1)) / t; /* Power */
    s0_power[chan][newHistPos] = p < INT16_MAX ? p : INT16_MAX;
    s0_time[chan][newHistPos] = 1;
    if(s0_time[chan][oldHistPos] != 0) {
      if(t >= 1<<10) {
        s0_time[chan][oldHistPos] = t >> 10;
      } else {
        s0_time[chan][oldHistPos] = 1;
      }
    }
    s0_histPos[chan] = newHistPos;
  }
}
void IRAM_ATTR interruptRoutine_1(void) { interruptRoutine(0); }
void IRAM_ATTR interruptRoutine_2(void) { interruptRoutine(1); }

void s0_EnableInterrupt(const uint8_t chan) {
  if(chan < S0_CHANS) {
    s0_bounce[chan] = currentMillis - UINT16_MAX;
  }
  switch(chan) {
    case 0:
      pinMode(S0_PIN1, INPUT_PULLUP);
      attachInterrupt(digitalPinToInterrupt(S0_PIN1), interruptRoutine_1, FALLING);
      break;
    case 1:
      pinMode(S0_PIN2, INPUT_PULLUP);
      attachInterrupt(digitalPinToInterrupt(S0_PIN2), interruptRoutine_2, FALLING);
      break;
  }
}
void s0_EnableInterrupts(void) {
  for(uint8_t chan = 0; chan < S0_CHANS; chan++) {
    s0_EnableInterrupt(chan);
  }
}
void s0_DisableInterrupt(const uint8_t chan) {
  switch(chan) {
    case 0:
      detachInterrupt(digitalPinToInterrupt(S0_PIN1));
      break;
    case 1:
      detachInterrupt(digitalPinToInterrupt(S0_PIN2));
      break;
  }
}
void s0_DisableInterrupts(void) {
  for(uint8_t chan = 0; chan < S0_CHANS; chan++) {
    s0_DisableInterrupt(chan);
  }
}



/* One-time setup */
void s0_Setup(void) {
  s0_EnableInterrupts();
}

/* Periodic loop */
void s0_Loop(void) {
  s0_ValidatePower();
}
