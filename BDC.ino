#include <EEPROM.h>
#include <DS1307new.h>
#include <avr/wdt.h>
#include "EtherShield.h"


#define DBG
#define DBGLED 4

#define MYWWWPORT 80
#define WEB_BUFFER_SIZE 1000
static uint8_t network_mac[6];
static uint8_t network_ip[4];

#define ALARMZ_SIZE 8
static uint8_t hourz[ALARMZ_SIZE];
static uint8_t minutez[ALARMZ_SIZE];
static uint8_t soundz[ALARMZ_SIZE];

#define NUM_OF_SOUNDZ 4
const prog_char sound_name_0[] PROGMEM = "none";
const prog_char sound_name_1[] PROGMEM = "Happy";
const prog_char sound_name_2[] PROGMEM = "Sad";
const prog_char sound_name_3[] PROGMEM = "Frei Tamas";
const char * const sound_name_table[] PROGMEM = {   
  sound_name_0,
  sound_name_1,
  sound_name_2,
  sound_name_3,
};

#define BUZZER 3
#define RESETBUTTON 5

const prog_char HTTP200OK[] PROGMEM = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nPragma: no-cache\r\n\r\n";
const prog_char HTTP401UNAUTHORIZED[] PROGMEM = "HTTP/1.0 401 Unauthorized\r\nContent-Type: text/html\r\n\r\n<h1>401 Unauthorized</h1>";

const prog_char HTML_HEADER_GENERAL[] PROGMEM = "<html><head><title>Big Display Clock</title></head><body><center><h1>Big Display Clock</h1><br>";
const prog_char HTML_FOOTER_GENERAL[] PROGMEM = "</center><hr></body></html>";
    
uint8_t current_hour = 255;
uint8_t current_min = 255;
uint8_t do_reboot = 0;

// The ethernet shield
EtherShield es=EtherShield();

void(* resetFunc) (void) = 0;//declare reset function at address 0

int freeRam() {
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}

void setup(){
  wdt_enable(WDTO_8S);

#ifdef DBGLED
  pinMode(DBGLED, OUTPUT);
#endif

  pinMode(RESETBUTTON, INPUT_PULLUP);

  Serial.begin(9600);

  if (digitalRead(RESETBUTTON) == LOW) {
    reset_network_eeprom();
    reset_alarm_eeprom();
  }

  get_alarm_eeprom();
  if (!get_network_eeprom()) {
    reset_network_eeprom();
    get_network_eeprom();
  }
#ifdef DBG
  print_config(network_mac, network_ip);
#endif
  // Initialise SPI interface
  es.ES_enc28j60SpiInit();

  // initialize enc28j60
  es.ES_enc28j60Init(network_mac);

  // init the ethernet/ip layer:
  es.ES_init_ip_arp_udp_tcp(network_mac, network_ip, MYWWWPORT);
  
  RTC.startClock();

}

#ifdef DBG
void print_config(uint8_t *mac, uint8_t *ip) {
  char temp[30];
  sprintf_P(temp,PSTR("MAC %02X:%02X:%02X:%02X:%02X:%02X"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.println(temp);
  sprintf_P(temp,PSTR("IP  %u.%u.%u.%u"), ip[0], ip[1], ip[2], ip[3]);
  Serial.println(temp);
}
#endif

#define EEPROM_NETWORK_OFFSET 0

boolean calc_network_checksum(uint8_t *mac, uint8_t *ip, uint8_t *sum) {
  uint8_t magic = 0xAA;
  uint8_t imagic;
  for (uint8_t i=0; i<6; i++) {
    magic ^= mac[i];
    magic += i;
  }
  for (uint8_t i=0; i<4; i++) {
    magic ^= ip[i];
    magic += i;
  }
  imagic = (~magic)&0xFF;
  if ((sum[0] == magic) && (sum[1] == imagic)) {
    return true;
  } else {
    sum[0] = magic;
    sum[1] = imagic;
    return false;
  }
}

void set_network_eeprom(uint8_t *mac, uint8_t *ip) {
  uint8_t cs[2];
  calc_network_checksum(mac, ip, cs);
  int base_addr;
  base_addr = EEPROM_NETWORK_OFFSET;
  for (uint8_t i=0; i<6; i++) {
    EEPROM.write(base_addr + i, mac[i]);
  }
  base_addr = EEPROM_NETWORK_OFFSET + 6;
  for (uint8_t i=0; i<4; i++) {
    EEPROM.write(base_addr + i, ip[i]);
  }
  base_addr = EEPROM_NETWORK_OFFSET + 10;
  for (uint8_t i=0; i<2; i++) {
    EEPROM.write(base_addr + i, cs[i]);
  }
}

void reset_network_eeprom() {
  uint8_t network_mac_default[6] = { 0x72,0x3C,0x39,0x00,0x18,0x00 };
  uint8_t network_ip_default[4] = { 192,168,0,1 };
  set_network_eeprom(network_mac_default, network_ip_default);
}

boolean get_network_eeprom() {
  uint8_t network_checksum[2];
  int base_addr;
  base_addr = EEPROM_NETWORK_OFFSET;
  for (uint8_t i=0; i<6; i++) {
    network_mac[i] = EEPROM.read(base_addr + i);
  }
  base_addr = EEPROM_NETWORK_OFFSET + 6;
  for (uint8_t i=0; i<4; i++) {
    network_ip[i] = EEPROM.read(base_addr + i);
  }
  base_addr = EEPROM_NETWORK_OFFSET + 10;
  for (uint8_t i=0; i<2; i++) {
    network_checksum[i] = EEPROM.read(base_addr + i);
  }
  return calc_network_checksum(network_mac, network_ip, network_checksum);
}

#define EEPROM_ALARM_OFFSET 12
void set_alarm_eeprom(uint8_t clockno, uint8_t hour, uint8_t min, uint8_t sound) {
  int base_addr = EEPROM_ALARM_OFFSET + clockno*3;
  EEPROM.write(base_addr + 0, hour);
  EEPROM.write(base_addr + 1, min);
  EEPROM.write(base_addr + 2, sound);
}

void get_alarm_eeprom() {
  for (uint8_t i=0; i<ALARMZ_SIZE; i++) {
    int base_addr = EEPROM_ALARM_OFFSET + i*3;
    hourz[i] = EEPROM.read(base_addr + 0) % 24;
    minutez[i] = EEPROM.read(base_addr + 1) % 60;
    soundz[i] = EEPROM.read(base_addr + 2) % NUM_OF_SOUNDZ;
  }
}

void reset_alarm_eeprom() {
  for (uint8_t i=0; i<ALARMZ_SIZE; i++) {
     int base_addr = EEPROM_ALARM_OFFSET + i*3;
     EEPROM.write(base_addr + 0, 0);
     EEPROM.write(base_addr + 1, 0);
     EEPROM.write(base_addr + 2, 0);
  }
}

uint16_t webpages_job_setip(char *params, uint8_t *buf) {
  char* paramstart;
  if ((paramstart = strstr(params,"?")) != 0) {
    char kvalstrbuf[16];
    if (es.ES_find_key_val(paramstart,kvalstrbuf,16,"SET")) {
      uint8_t newip[4];
      if (es.ES_find_key_val(paramstart,kvalstrbuf,16,"I1")) { newip[0] = strtoul(kvalstrbuf, (char**)0, 10) % 256; }
      if (es.ES_find_key_val(paramstart,kvalstrbuf,16,"I2")) { newip[1] = strtoul(kvalstrbuf, (char**)0, 10) % 256; }
      if (es.ES_find_key_val(paramstart,kvalstrbuf,16,"I3")) { newip[2] = strtoul(kvalstrbuf, (char**)0, 10) % 256; }
      if (es.ES_find_key_val(paramstart,kvalstrbuf,16,"I4")) { newip[3] = strtoul(kvalstrbuf, (char**)0, 10) % 256; }
      set_network_eeprom(network_mac, newip);
      do_reboot = 1;
    }
  }
  char value[16];
  uint16_t plen;
  plen=es.ES_fill_tcp_data_p(buf,0,HTTP200OK);
  plen=es.ES_fill_tcp_data_p(buf,plen,HTML_HEADER_GENERAL);
  if (!do_reboot) {
    plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<h3>Set IP</h3><br>"));
    plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<form method=GET>"));
    plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<input type=text size=3 name='I1' value='"));
    sprintf(value,"%d",network_ip[0]); plen=es.ES_fill_tcp_data(buf,plen,value);
    plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("'>."));
    plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<input type=text size=3 name='I2' value='"));
    sprintf(value,"%02d",network_ip[1]); plen=es.ES_fill_tcp_data(buf,plen,value);
    plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("'>."));
    plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<input type=text size=3 name='I3' value='"));
    sprintf(value,"%02d",network_ip[2]); plen=es.ES_fill_tcp_data(buf,plen,value);
    plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("'>."));
    plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<input type=text size=3 name='I4' value='"));
    sprintf(value,"%02d",network_ip[3]); plen=es.ES_fill_tcp_data(buf,plen,value);
    plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("'>"));
    plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<input type=submit name=SET value=Ok></form>"));
    plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<a href='/'>Main</a>"));
  } else {
    plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("IP set done, rebooting..."));
  }
  plen=es.ES_fill_tcp_data_p(buf,plen,HTML_FOOTER_GENERAL);
  return plen;
}

uint16_t webpages_job_setclock(char *params, uint8_t *buf) {
  char* paramstart;
  if ((paramstart = strstr(params,"?")) != 0) {
    char kvalstrbuf[16];
    if (es.ES_find_key_val(paramstart,kvalstrbuf,16,"SET")) {
      RTC.stopClock();
      if (es.ES_find_key_val(paramstart,kvalstrbuf,16,"Y")) { RTC.year = strtoul(kvalstrbuf, (char**)0, 10); }
      if (es.ES_find_key_val(paramstart,kvalstrbuf,16,"MO")) { RTC.month = strtoul(kvalstrbuf, (char**)0, 10); }
      if (es.ES_find_key_val(paramstart,kvalstrbuf,16,"D")) { RTC.day = strtoul(kvalstrbuf, (char**)0, 10); }
      if (es.ES_find_key_val(paramstart,kvalstrbuf,16,"H")) { RTC.hour = strtoul(kvalstrbuf, (char**)0, 10); }
      if (es.ES_find_key_val(paramstart,kvalstrbuf,16,"MI")) { RTC.minute = strtoul(kvalstrbuf, (char**)0, 10); }
      RTC.second = 0;
      RTC.setTime();
      RTC.startClock();
    }
  }
  char value[16];
  uint16_t plen;
  plen=es.ES_fill_tcp_data_p(buf,0,HTTP200OK);
  plen=es.ES_fill_tcp_data_p(buf,plen,HTML_HEADER_GENERAL);
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<h3>Set clock</h3><br>"));
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<form method=GET>"));
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<input type=text size=4 name='Y' value='"));
  sprintf(value,"%d",RTC.year); plen=es.ES_fill_tcp_data(buf,plen,value);
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("'>/"));
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<input type=text size=2 name='MO' value='"));
  sprintf(value,"%02d",RTC.month); plen=es.ES_fill_tcp_data(buf,plen,value);
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("'>/"));
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<input type=text size=2 name='D' value='"));
  sprintf(value,"%02d",RTC.day); plen=es.ES_fill_tcp_data(buf,plen,value);
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("'><br>"));
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<input type=text size=2 name='H' value='"));
  sprintf(value,"%02d",RTC.hour); plen=es.ES_fill_tcp_data(buf,plen,value);
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("'>:"));
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<input type=text size=2 name='MI' value='"));
  sprintf(value,"%02d",RTC.minute); plen=es.ES_fill_tcp_data(buf,plen,value);
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("'><br>"));
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<input type=submit name=SET value=Ok></form>"));
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<a href='/'>Main</a>"));
  plen=es.ES_fill_tcp_data_p(buf,plen,HTML_FOOTER_GENERAL);
  return plen;
}

uint16_t webpages_job_clocks(char *params, uint8_t *buf) {
  char* paramstart;
  uint8_t clockno = 0;
  uint8_t tmp_Set = 0;
  if ((paramstart = strstr(params,"?")) != 0) {
    int i;
    char par[16];
    char kvalstrbuf[16];

    uint8_t tmp_H, tmp_M, tmp_S;
    if (es.ES_find_key_val(paramstart,kvalstrbuf,16,"H")) {
      tmp_H = strtoul(kvalstrbuf, (char**)0, 10);
      tmp_H = tmp_H % 24;
    }
    if (es.ES_find_key_val(paramstart,kvalstrbuf,16,"M")) {
      tmp_M = strtoul(kvalstrbuf, (char**)0, 10);
      tmp_M = tmp_M % 60;
    }
    if (es.ES_find_key_val(paramstart,kvalstrbuf,16,"S")) {
      tmp_S = strtoul(kvalstrbuf, (char**)0, 10);
      tmp_S = tmp_S % NUM_OF_SOUNDZ;
    }
    if (es.ES_find_key_val(paramstart,kvalstrbuf,16,"C")) {
      clockno = strtoul(kvalstrbuf, (char**)0, 10);
      clockno = clockno % ALARMZ_SIZE;
    }
    if (es.ES_find_key_val(paramstart,kvalstrbuf,16,"SET")) {
      tmp_Set = 1;
    }

    if (tmp_Set) {
      hourz[clockno] = tmp_H;
      minutez[clockno] = tmp_M;
      soundz[clockno] = tmp_S;
      set_alarm_eeprom(clockno, tmp_H, tmp_M, tmp_S);
    }

  }

  char value[16];

  uint16_t plen;
  plen=es.ES_fill_tcp_data_p(buf,0,HTTP200OK);
  plen=es.ES_fill_tcp_data_p(buf,plen,HTML_HEADER_GENERAL);
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<h3>Set alarm</h3><br>"));
  if (tmp_Set) {
    plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("Set OK"));
  }
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<br>"));
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<form method=GET>"));
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<input type=text size=2 name='H' value='"));
  sprintf(value,"%02d",hourz[clockno]); plen=es.ES_fill_tcp_data(buf,plen,value);
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("'>:"));
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<input type=text size=2 name='M' value='"));
  sprintf(value,"%02d",minutez[clockno]); plen=es.ES_fill_tcp_data(buf,plen,value);
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("'><br>"));
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<select name=S size=1>"));
  for (uint8_t i=0; i<NUM_OF_SOUNDZ; i++) {
    plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<option value='"));
    sprintf(value,"%d",i); plen=es.ES_fill_tcp_data(buf,plen,value);
    plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("' "));
    if (i == soundz[clockno]) { plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("selected"));}
    plen=es.ES_fill_tcp_data_p(buf,plen,PSTR(">"));
    char soundname[20];
    strcpy_P(soundname, (char*)pgm_read_word(&(sound_name_table[i])));
    plen=es.ES_fill_tcp_data(buf,plen,soundname);
  }
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("</select><br>"));
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<input type=hidden name=C value='"));
  sprintf(value,"%d",clockno);
  plen=es.ES_fill_tcp_data(buf,plen,value);
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("'>"));
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<input type=submit name=SET value=Ok></form>"));  
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<a href='/'>Main</a>"));
  plen=es.ES_fill_tcp_data_p(buf,plen,HTML_FOOTER_GENERAL);

  return plen;
}

uint16_t webpages_job_main(char *params, uint8_t *buf) {
  uint16_t plen;
  plen=es.ES_fill_tcp_data_p(buf,0,HTTP200OK);
  plen=es.ES_fill_tcp_data_p(buf,plen,HTML_HEADER_GENERAL);
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("<h3>Main page</h3><br><table>"));
  for (uint8_t i=0; i<ALARMZ_SIZE; i++) {
    char soundname[20];
    strcpy_P(soundname, (char*)pgm_read_word(&(sound_name_table[soundz[i]])));
    char value[80];
    sprintf_P(value,PSTR("<tr><td><a href='/clocks?C=%d'>%d: "),i,i+1);
    plen=es.ES_fill_tcp_data(buf,plen,value);
    sprintf_P(value,PSTR("%02d:%02d</a></td><td>%s</td></tr>"),hourz[i],minutez[i],soundname);
    plen=es.ES_fill_tcp_data(buf,plen,value);
  }
  plen=es.ES_fill_tcp_data_p(buf,plen,PSTR("</table><br><br><a href='/setclock'>Set clock</a>"));
  plen=es.ES_fill_tcp_data_p(buf,plen,HTML_FOOTER_GENERAL);
  return plen;
}

bool webpage_name(char *params, char *pagename) {
  char compare[16];
  sprintf_P(compare,PSTR("/%s "),pagename);
  if (strncmp(compare,params,strlen(pagename)+2)==0) { return true; }
  sprintf_P(compare,PSTR("/%s?"),pagename);
  if (strncmp(compare,params,strlen(pagename)+2)==0) { return true; }
  return false;
}

uint16_t webpages_job(char *params, uint8_t *buf) {
  if (webpage_name(params,"")) {
     return webpages_job_main(params, buf);
  } else if (webpage_name(params,"clocks")) {
     return webpages_job_clocks(params, buf);
  } else if (webpage_name(params,"setclock")) {
     return webpages_job_setclock(params, buf);
  } else if (webpage_name(params,"setip")) {
     return webpages_job_setip(params, buf);
  } else {
     return es.ES_fill_tcp_data_p(buf,0,HTTP401UNAUTHORIZED);
  }
}

void web_loop() {
  uint8_t buf[WEB_BUFFER_SIZE+1];
  uint16_t dat_p;
  memset(buf,0,WEB_BUFFER_SIZE);
  dat_p=es.ES_packetloop_icmp_tcp(buf,es.ES_enc28j60PacketReceive(WEB_BUFFER_SIZE, buf));
  if(dat_p==0){ return; }

#ifdef DBG
  Serial.print("Incoming length:"); Serial.println(strlen((char *)&buf[dat_p])); //Serial.println("Incoming data:"); Serial.println((char *)&buf[dat_p]);
#endif
  if (strncmp("GET ",(char *)&(buf[dat_p]),4)!=0){
    // head, post and other methods:
    dat_p=es.ES_fill_tcp_data_p(buf,0,HTTP200OK);
    dat_p=es.ES_fill_tcp_data_p(buf,dat_p,PSTR("<h1>200 OK</h1>"));
    es.ES_www_server_reply(buf,dat_p); // send web page data
    return;
  }

  uint16_t plen = webpages_job((char*)&(buf[dat_p+4]),buf);
#ifdef DBG
  Serial.print("Outgoing length:"); Serial.println(plen); Serial.print("freemem="); Serial.println(freeRam());
#endif
  es.ES_www_server_reply(buf,plen);
  if (do_reboot) {
    resetFunc();
  }
}

void play_sound(uint8_t id) {
#ifdef DBG
  char soundname[20];
  strcpy_P(soundname, (char*)pgm_read_word(&(sound_name_table[id])));
  Serial.print("Play "); Serial.println(soundname);
#endif
  tone(BUZZER, 440);
  delay(200); wdt_reset();
  noTone(BUZZER);
  delay(200); wdt_reset();
  tone(BUZZER, 440);
  delay(200); wdt_reset();
  noTone(BUZZER);
  delay(200); wdt_reset();
  tone(BUZZER, 440);
  delay(200); wdt_reset();
  noTone(BUZZER);
  digitalWrite(BUZZER, LOW);
}

void play_sound_if_needed() {
  for (uint8_t i=0; i<ALARMZ_SIZE; i++) {
    if ((soundz[i] != 0) && (hourz[i] == current_hour) && (minutez[i] == current_min)) {
      play_sound(soundz[i]);
      return;
    }
  }
}

void update_big_display(uint8_t hour, uint8_t min) {
  // TODO
}

void time_loop() {
  RTC.getTime();
  if ((RTC.hour == 3) && (RTC.minute == 14) && (RTC.second == 59)) {
      // Reboot every early morning
      resetFunc();
  }
#ifdef DBGLED
  if (RTC.second % 2) {
    digitalWrite(DBGLED, HIGH);
  } else {
    digitalWrite(DBGLED, LOW);
  }
#endif
  if ((current_hour != RTC.hour) || (current_min != RTC.minute)) {
#ifdef DBG
    Serial.print("TIMESTEP ");
    Serial.print(RTC.hour); Serial.print(":"); Serial.print(RTC.minute); Serial.print(":"); Serial.print(RTC.second); Serial.println();
#endif
    current_hour = RTC.hour;
    current_min = RTC.minute;
    update_big_display(current_hour, current_min);
    play_sound_if_needed();
  }
}

void loop(){
  wdt_reset();
  web_loop();
  time_loop();
}
