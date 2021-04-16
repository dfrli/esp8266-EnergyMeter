#include <ESP8266WebServer.h>
#include <ESP8266WebServerSecure.h>
#include <ESP8266HTTPUpdateServer.h>

/* HTTP URLs:
 * / .  .  .  .  .  .  . HTML Index Document
 * /json   .  .  .  .  . JSON Document
 * /s0/<n> .  .  .  .  . S0 channel (n >= 1)
 *        /clr   .  .  . Clear Meter Counters
 *        /mtr   .  .  . Current Meter Value [Wh]
 *        /age   .  .  . Age of last meassurement [s]
 *        /pwr   .  .  . Current Power [W]
 *            (?avg=n) . Average power [W] of last n meassurements or seconds
 * /sdm/<n>   .  .  .  . SDM device (n >= 1)
 *         /imp  .  .  . Import Meter [Wh]
 *         /exp  .  .  . Export Meter [Wh]
 *         /sum  .  .  . Total consumption (import - export) [Wh]
 *         /pwr  .  .  . Current Power [W]
 *             (?avg=n)  Average power of last n meassurements or seconds
 * /sml .  .  .  .  .  . SML Meter
 *         /imp  .  .  . Import Meter [Wh]
 *         /exp  .  .  . Export Meter [Wh]
 *         /sum  .  .  . Total consumption (import - export) [Wh]
 *         /pwr  .  .  . Current Power [W]
 *             (?avg=n)  Average power of last n seconds
 * /update .  .  .  .  . OTA Firmware Update
 */

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

/* Constants for OTA firmware update */
static const char* const httpUpdatePath     = "/update";
static const char* const httpUpdateUsername = "update";
static const char* const httpUpdatePassword = "OTA";

/* Strings for common MIME Types */
static const char* const httpMimeTypeHtml = "text/html";
static const char* const httpMimeTypeText = "text/plain";
static const char* const httpMimeTypeJson = "application/json";

/* Serve HTTP 404 */
void http_NotFound(void) {
  httpServer.send(404);
}

/* Send HTTP header to limit caching up to maxAge [s] or no caching at all (0) */
void http_Expires(const uint32_t maxAge) {
  const String httpHeader = F("Cache-Control");
  if(maxAge > 0) {
    String headerCacheControlValue((char*)0);
    headerCacheControlValue.reserve(30);
    headerCacheControlValue += F("public, max-age=");
    headerCacheControlValue += maxAge;
    httpServer.sendHeader(httpHeader, headerCacheControlValue);
  }
  else {
    httpServer.sendHeader(httpHeader, F("no-cache, no-store, must-revalidate"));
  }
}

/* Serve index document */
void http_serve_Index(void) {
  String document((char*)0); /* String Builder */
  document.reserve(5000);
  document += F(
"<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"<meta charset=\"iso8859-15\">\n"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"<link rel=\"shortcut icon\" href=\"data:image/png;base64,"
"iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAQAAADZc7J/AAABA0lEQVRIx+2VPXLCMBCFP2cY"
"H4DOKTgIHCE5jicXCMRwJGCcxnCTOAeAFHaTpcA/SF5LIlUK3haydrSr91YrGR7o8UrOD+Kx"
"MzkvWvjSG3pr78PdhYqUxMszIaVCbBafCGmw2DeE3HSdEBLgOCBbKAmeEU7tJAJAmi9RlkeK"
"T3r/kxLQW4uYDd+UZMS6Kmn2FotDO193kj7UdUYC2wBKhDkLhNJOYEo4DLgVDKvlkDCGrOOz"
"cksYQ0xGyRerpojBCY5qHzhqYOPXwyxYwp8ZBMBMMGNHPXqJa3bM3JT23pdg6z6FGmGqtPZ1"
"nCJU/la+ZwQmxhUNOwGliIe7i188/kL/CReSGcn779iUfAAAAABJRU5ErkJggg==\">\n"
"<title>Energy Meter</title>\n"
"<style>\n"
"th { padding:1ex; text-align:center; font-weight:bold; }\n"
"td { padding:1ex; text-align:right; }\n"
"a { color:black; text-decoration:none; }\n"
"a:hover { color:blue; text-decoration:none; }\n"
"</style>\n"
"<script>\n"
"var lang = navigator.language || navigator.userLanguage;\n"
"var tSep = ','; var dSep = '.';\n"
"if(lang.substring(0,2) == \"de\") { tSep = '.'; dSep = ','; }\n"
"function clr(e) {\n"
"if(window.confirm(\"Clear Counters?\")) {\n"
"var url = e.href;\n"
"var xhttp = new XMLHttpRequest();\n"
"xhttp.onreadystatechange = function() {\n"
"if(this.readyState == 4 && this.status == 200) {\n"
"location.reload();\n"
"}\n"
"};\n"
"xhttp.open(\"GET\", url, true);\n"
"xhttp.send();\n"
"}\n"
"}\n"
"function numberFormat(num) {\n"
"var parts = num.toString().split(\".\");\n"
"parts[0] = parts[0].replace(/\\B(?=(\\d{3})+(?!\\d))/g, tSep);\n"
"return parts.join(dSep).replace(/-/g, \"&minus;\");\n"
"}\n"
"</script>\n"
"</head>\n"
"<body>\n"
"<h1>Energy Meter</h1>\n"
"<table>\n"
"<tr>\n"
"<th>ID</th>\n"
"<th>Power [W]</th>\n"
"<th>Import [Wh]</th>\n"
"<th>Export [Wh]</th>\n"
"</tr>\n"
"<tr id=\"sml\"");
  if(!sml_hasValue()) {
    document += F(" style=\"display:none\"");
  }
  document += F(">\n"
"<td></td>\n"
"<td><a href=\"/sml/pwr\"><span id=\"sml_pwr\">");
  document += sml_power;
  document += F("</span></a></td>\n"
"<td><a href=\"/sml/imp\"><span id=\"sml_imp\">");
  document += sml_import;
  document += F("</span></a></td>\n"
"<td><a href=\"/sml/exp\"><span id=\"sml_exp\">");
  document += sml_export;
  document += F("</span></a></td>\n"
"</tr>\n");
  for(uint8_t i = 0; i < SDM_MAX_ID; i++) {
    const uint8_t chan = i + 1;
    document += F("<tr id=\"sdm");
    document += chan;
    document += F("\"");
    if(!sdm_hasValue(i)) {
      document += F(" style=\"display:none\"");
    }
    document += F(">\n"
"<td>");
    document += chan;
    document += F("</td>\n"
"<td><a href=\"/sdm/");
    document += chan;
    document += F("/pwr\"><span id=\"sdm_");
    document += chan;
    document += F("_pwr\">");
    document += (int)sdm_power[i];
    document += F("</span></a></td>\n"
"<td><a href=\"/sdm/");
    document += chan;
    document += F("/imp\"><span id=\"sdm_");
    document += chan;
    document += F("_imp\">");
    document += (int)(sdm_import[i]*1000);
    document += F("</span></a></td>\n"
"<td><a href=\"/sdm/");
    document += chan;
    document += F("/exp\"><span id=\"sdm_");
    document += chan;
    document += F("_exp\">");
    document += (int)(sdm_export[i]*1000);
    document += F("</span></a></td>\n"
"</tr>\n");
  }
  document += F(
"</table>\n"
"<h1>S0 Counter</h1>\n"
"<table>\n"
"<tr>\n"
"<th>Channel</th>\n"
"<th>Meter [Wh]</th>\n"
"<th>Power [W]</th>\n"
"<th colspan=\"3\">Average [W]</th>\n"
"<th>Clear</th>\n"
"</tr>\n");
  for(uint8_t i = 0; i < S0_CHANS; i++) {
    const uint8_t chan = i + 1;
    document += F("<tr id=\"row");
    document += chan;
    document += F("\"");
    if(!s0_hasValue(i)) {
      document += F(" style=\"display:none\"");
    }
    document += F(">\n"
"<td><a href=\"/s0/");
    document += chan;
    document += F("\"><span id=\"chan");
    document += chan;
    document += F("\">");
    document += chan;
    document += F("</span></a></td>\n"
"<td><a href=\"/s0/");
    document += chan;
    document += F("/mtr\"><span id=\"mtr");
    document += chan;
    document += F("\">");
    document += s0_energy[i];
    document += F("</span></a></td>\n"
"<td><a href=\"/s0/");
    document += chan;
    document += F("/pwr\"><span id=\"pwr");
    document += chan;
    document += F("\">");
    document += s0_power[i][s0_histPos[i]];
    document += F("</span></a></td>\n"
"<td><a href=\"/s0/");
    document += chan;
    document += F("/pwr?avg=2\"><span id=\"avg");
    document += chan;
    document += F("\">");
    document += s0_PowerAverage(i, 2);
    document += F("</span></a></td>\n"
"<td><a href=\"/s0/");
    document += chan;
    document += F("/pwr?avg=4\"><span>");
    document += s0_PowerAverage(i, 4);
    document += F("</span></a></td>\n"
"<td><a href=\"/s0/");
    document += chan;
    document += F("/pwr?avg=8\"><span>");
    document += s0_PowerAverage(i, 8);
    document += F("</span></a></td>\n"
"<td><a href=\"/s0/");
    document += chan;
    document += F("/clr\" onclick=\"clr(this);return false;\">&squ;</a></td>\n"
"</tr>\n");
  }
  document += F(
"</table>\n"
"<script>\n"
"for(chan=1;chan<=");
  document += SDM_MAX_ID;
  document += F(";chan++) {\n"
"if(document.getElementById(\"sdm_\"+chan+\"_imp\").innerHTML == \"0\""
" && document.getElementById(\"sdm_\"+chan+\"_exp\").innerHTML == \"0\")"
" document.getElementById(\"sdm\"+chan).style.display = \"none\";\n"
"}\n"
"for(chan=1;chan<=");
  document += S0_CHANS;
  document += F(";chan++) {\n"
"if(document.getElementById(\"mtr\"+chan).innerHTML == \"0\")"
" document.getElementById(\"row\"+chan).style.display = \"none\";\n"
"}\n"
"var nums = document.getElementsByTagName(\"span\");\n"
"for(i=0;i<nums.length;i++) {\n"
"nums[i].innerHTML = numberFormat(nums[i].innerHTML);\n"
"}\n"
"</script>\n"
"</body>\n");
  http_Expires(0);
  httpServer.send(200, httpMimeTypeHtml, document);
}

/* Serve JSON document */
void http_serve_Json() {
  String document((char*)0);
  document.reserve(/* JSON */ 50 + /* SML */ 100
    /* S0 */ + 50 * S0_CHANS + /*AVG*/ 25 * S0_CHANS
    #ifdef DEBUG
    + /*TS*/ 25 * S0_HIST_SIZE * S0_CHANS + /*PWR*/ 25 * S0_HIST_SIZE * S0_CHANS
    #endif
    /* SDM */ + 100 * SDM_MAX_ID);
  document += F(
"{\n"
"  \"S0\":\n"
"  {\n");
  for(uint8_t i = 0; i < S0_CHANS; i++) {
    const uint8_t chan = i + 1;
    const bool s0_valid = s0_hasValue(i);
    document += F(
"    \"");
    document += chan;
    document += F("\":\n"
"    {\n"
"      \"pwr\": ");
    if(s0_valid) document += s0_power[i][s0_histPos[i]]; else document += "null";
    document += F(",\n"
"      \"avg\": ");
    if(s0_valid) document += s0_PowerAverage(i, 60); else document += "null";
    document += F(",\n");
    #ifdef DEBUG
    for(uint8_t j = 1; j < S0_HIST_SIZE && j <= 8; j++) {
      document += F(
"      \"pwr");
      document += j;
      document += F("\": ");
      if(s0_valid) document += s0_power[i][j]; else document += "null";
      document += F(",\n");
    }
    for(uint8_t j = 1; j < S0_HIST_SIZE && j <= 8; j++) {
      document += F(
"      \"ts");
      document += j;
      document += F("\": ");
      if(s0_valid) document += s0_time[i][j]; else document += "null";
      document += F(",\n");
    }
    #endif
    document += F(
"      \"mtr\": ");
    if(s0_valid) document += s0_energy[i]; else document += "null";
    document += F(",\n"
"      \"age\": ");
    if(s0_valid) document += s0_Age(i); else document += "null";
    document += F("\n"
"    }");
    if(chan < S0_CHANS) {
      document += ',';
    }
    document += '\n';
  }
  document += F(
"  },\n"
"  \"SDM\":\n"
"  {\n");
  for(uint8_t i = 0; i < SDM_MAX_ID; i++) {
    const uint8_t chan = i + 1;
    const bool sdm_valid = sdm_hasValue(i);
    document += F(
"    \"");
    document += chan;
    document += F("\":\n"
"    {\n"
"      \"pwr\": ");
    if(sdm_valid) document += (int)sdm_power[i]; else document += "null";
    document += F(",\n"
"      \"imp\": ");
    if(sdm_valid) document += (int)(sdm_import[i]*1000); else document += "null";
    document += F(",\n"
"      \"exp\": ");
    if(sdm_valid) document += (int)(sdm_export[i]*1000); else document += "null";
    document += F("\n"
"    }");
    if(chan < SDM_MAX_ID) {
      document += ',';
    }
    document += '\n';
  }
  document += F(
"  },\n"
"  \"SML\":\n"
"  {\n"
"    \"imp\": ");
  const bool sml_valid = sml_hasValue();
  if(sml_valid) document += sml_import; else document += "null";
  document += F(",\n"
"    \"exp\": ");
  if(sml_valid) document += sml_export; else document += "null";
  document += F(",\n"
"    \"pwr\": ");
  if(sml_valid) document += sml_power; else document += "null";
  document += F("\n"
"  }\n"
"}\n");
  http_Expires(0);
  httpServer.send(200, httpMimeTypeJson, document);
}

/* S0 URLs */

/* /s0/<n> */  /* Meter Value */
void http_serve_S0(const uint8_t chan) {
  if(chan < S0_CHANS) {
    http_Expires(0);
    httpServer.send(200, httpMimeTypeText, s0_hasValue(chan) ? String(s0_energy[chan]) : "");
  } else {
    httpServer.send(404);
  }
}

/* /s0/<n>/pwr(?avg=n) */  /* Current (or historical average) Power */
void http_serve_S0Pwr(const uint8_t chan, uint32_t avg) {
  if(chan < S0_CHANS) {
    if(avg == 0) {
      for(int i = 0; i < httpServer.args(); i++) {
        if(httpServer.argName(i) == F("avg")) {
          avg = httpServer.arg(i).toInt();
        }
      }
    }
    http_Expires(0);
    httpServer.send(200, httpMimeTypeText, s0_hasValue(chan) ? String(s0_PowerAverage(chan, avg)) : "");
  } else {
    httpServer.send(404);
  }
}

/* /s0/<n>/age */  /* Age of last meassurement */
void http_serve_S0Age(const uint8_t chan) {
  if(chan < S0_CHANS) {
    http_Expires(0);
    httpServer.send(200, httpMimeTypeText, s0_hasValue(chan) ? String(s0_Age(chan)) : "");
  } else {
    httpServer.send(404);
  }
}

/* /s0/<n>/clr */  /* Reset Meter Value */
void http_serve_S0Clr(const uint8_t chan) {
  if(chan < S0_CHANS) {
    if(s0_hasValue(chan)) {
      s0_ClearCounter(chan);
    }
    http_Expires(0);
    httpServer.send(200, httpMimeTypeText, String());
  } else {
    httpServer.send(404);
  }
}

/* SML URLs */

/* /sml */  /* Total consumption (import - export) */
void http_serve_Sml(void) {
  http_Expires(0);
  httpServer.send(200, httpMimeTypeText, sml_hasValue() ? String((int32_t)sml_import - (int32_t)sml_export) : "");
}

/* /sml/imp */  /* Import Meter */
void http_serve_SmlImp(void) {
  http_Expires(0);
  httpServer.send(200, httpMimeTypeText, sml_hasValue() ? String(sml_import) : "");
}

/* /sml/exp */  /* Export Meter */
void http_serve_SmlExp(void) {
  http_Expires(0);
  httpServer.send(200, httpMimeTypeText, sml_hasValue() ? String(sml_export) : "");
}

/* /sml/pwr(?avg=n) */  /* Current (or historical average) Power */
void http_serve_SmlPwr(void) {
  uint32_t avg = 0;
  for(int i = 0; i < httpServer.args(); i++) {
    if(httpServer.argName(i) == F("avg")) {
      avg = httpServer.arg(i).toInt();
    }
  }
  http_Expires(0);
  httpServer.send(200, httpMimeTypeText, sml_hasValue() ? String(sml_PowerAverage(avg)) : "");
}

/* SDM URLs */

/* /sdm/<n> */  /* Total consumption (import - export) */
void http_serve_Sdm(const uint8_t chan) {
  if(chan < SDM_MAX_ID) {
    http_Expires(0);
    httpServer.send(200, httpMimeTypeText, sdm_hasValue(chan) ?
      String((int)(sdm_import[chan]*1000) - (int)(sdm_export[chan]*1000)) : "");
  } else {
    httpServer.send(404);
  }
}

/* /sdm/<n>/pwr(?avg=n) */  /* Current (or historical average) Power */
void http_serve_SdmPwr(const uint8_t chan, uint32_t avg) {
  if(chan < SDM_MAX_ID) {
     if(avg == 0) {
      for(int i = 0; i < httpServer.args(); i++) {
        if(httpServer.argName(i) == F("avg")) {
          avg = httpServer.arg(i).toInt();
        }
      }
    }
    http_Expires(0);
    httpServer.send(200, httpMimeTypeText, sdm_hasValue(chan) ? String(sdm_PowerAverage(chan, avg)) : "");
  } else {
    httpServer.send(404);
  }
}

/* /sdm/<n>/imp */  /* Import Meter */
void http_serve_SdmImp(const uint8_t chan) {
  if(chan < SDM_MAX_ID) {
    http_Expires(0);
    httpServer.send(200, httpMimeTypeText, sdm_hasValue(chan) ? String((int)(sdm_import[chan]*1000)) : "");
  } else {
    httpServer.send(404);
  }
}

/* /sdm/<n>/exp */  /* Export Meter */
void http_serve_SdmExp(const uint8_t chan) {
  if(chan < SDM_MAX_ID) {
    http_Expires(0);
    httpServer.send(200, httpMimeTypeText, sdm_hasValue(chan) ? String((int)(sdm_export[chan]*1000)) : "");
  } else {
    httpServer.send(404);
  }
}



#ifdef DEBUG
void ICACHE_RAM_ATTR interruptRoutine(const uint8_t chan); /* declare external interrupt function */
#endif

/* One-time setup */
void http_Setup(void) {
  char url[10];
  /* / */
  httpServer.on("/", http_serve_Index);
  httpServer.on(F("/json"), http_serve_Json);
  /* /sml */
  memset(url, 0, sizeof(url));
  strcpy_P(url, PSTR("/sml"));
  httpServer.on(url, http_serve_Sml);
  url[4] = '/';
  strcpy_P(url+5, PSTR("sum"));
  httpServer.on(url, http_serve_Sml);
  strcpy_P(url+5, PSTR("imp"));
  httpServer.on(url, http_serve_SmlImp);
  strcpy_P(url+5, PSTR("exp"));
  httpServer.on(url, http_serve_SmlExp);
  strcpy_P(url+5, PSTR("pwr"));
  httpServer.on(url, http_serve_SmlPwr);
  /* /s0 */
  memset(url, 0, sizeof(url));
  strcpy_P(url, PSTR("/s0/"));
  for(uint8_t i = 0; i < S0_CHANS && i < 9; i++) {
    memset(url+4, 0, sizeof(url)-4);
    url[4] = '1' + i;
    httpServer.on(url, [i](){ http_serve_S0(i); });
    url[5] = '/';
    strcpy_P(url+6, PSTR("mtr"));
    httpServer.on(url, [i](){ http_serve_S0(i); });
    strcpy_P(url+6, PSTR("ctr"));
    httpServer.on(url, [i](){ http_serve_S0(i); });
    strcpy_P(url+6, PSTR("clr"));
    httpServer.on(url, [i](){ http_serve_S0Clr(i); });
    strcpy_P(url+6, PSTR("rst"));
    httpServer.on(url, [i](){ http_serve_S0Clr(i); });
    strcpy_P(url+6, PSTR("pwr"));
    httpServer.on(url, [i](){ http_serve_S0Pwr(i, 0); });
    strcpy_P(url+6, PSTR("age"));
    httpServer.on(url, [i](){ http_serve_S0Age(i); });
    #ifdef DEBUG
    strcpy_P(url+6, PSTR("dbg")); /* Simulate S0 impulse */
    httpServer.on(url, [i](){ interruptRoutine(i); httpServer.send(200, httpMimeTypeText, String()); });
    #endif
  }
  /* /sdm */
  memset(url, 0, sizeof(url));
  strcpy_P(url, PSTR("/sdm/"));
  for(uint8_t i = 0; i < SDM_MAX_ID && i < 9; i++) {
    memset(url+5, 0, sizeof(url)-5);
    url[5] = '1' + i;
    httpServer.on(url, [i](){ http_serve_Sdm(i); });
    url[6] = '/';
    strcpy_P(url+7, PSTR("imp"));
    httpServer.on(url, [i](){ http_serve_SdmImp(i); });
    strcpy_P(url+7, PSTR("exp"));
    httpServer.on(url, [i](){ http_serve_SdmExp(i); });
    strcpy_P(url+7, PSTR("sum"));
    httpServer.on(url, [i](){ http_serve_Sdm(i); });
    strcpy_P(url+7, PSTR("pwr"));
    httpServer.on(url, [i](){ http_serve_SdmPwr(i, 0); });
  }
  /* */
  httpServer.on(F("/free"), [](){ httpServer.send(200, httpMimeTypeText, String(ESP.getFreeHeap())); });
  httpServer.onNotFound(http_NotFound);
  httpUpdater.setup(&httpServer, httpUpdatePath, httpUpdateUsername, httpUpdatePassword);
  httpServer.begin();
}

/* Periodic Loop */
void http_Loop(void) {
  httpServer.handleClient();
}
