
// Definitions
#define SERVER_WEBPORT 80
#define SERIAL_BAUDRATE 115200
#define REQUEST_MAXBUFFER 30
#define HEADER_MAXBUFFER 20

// Compilation options
//#define DEBUG_EEPROM
//#define DEBUG
//#define DEBUG_HTTP
#define VERBOSE
#define ALLOW_CACHE

// Includes
#include <SPI.h>
#include <SD.h>
#include <Ethernet.h>
#include <EEPROM.h>

EthernetClient client;

// Request declarions
char request [REQUEST_MAXBUFFER];

// Initialize the Ethernet server library
// with the IP address and port you want to use 
// (port 80 is default for HTTP):
EthernetServer server(SERVER_WEBPORT);

/*****************************************************************************************************
 *                                                  EEPROM                                           *
 *****************************************************************************************************/
// EEPROM BASE ADDR
#define _EEPROM_BASE   0x10

// Define input/output
#define _INPUT         'i'
#define _OUTPUT        'o'
#define _NAME          'n'
#define _ADDR          'a'

// Analog inputs
#define _EMPTY         'e'
#define _RAW           'r'
#define _TEMPERATURE   't'
#define _POTENCIOMETER 'p'
#define _LIGHT         'l'

// Outputs
#define _LOGICAL       'b'  // 'b' of binary
#define _PWM           'w'  // 'w' of width

// Header options
#define _REQ_OK        0x01
#define _NOT_MODIFIED  0x02

// Configuration structure
struct Configuration{
  char inputs [6];
  char outputs [6];
  byte ip;
  char devName[16];
};
Configuration conf;

/*****************************************************************************************************
 *                                            Request Read                                           *
 *****************************************************************************************************/
byte requestRead (EthernetClient client) {
  char header [HEADER_MAXBUFFER];
  boolean currentLineIsBlank = true;
  boolean firstLine = true;
  byte reqLen = 0;
  byte headLen = 0;
  byte val = 0x00;
  
  while (client.connected() && client.available()) {
    // Read header
    char c = client.read();
    
    // Store in request only the first line with the httpmethod and the path
    if (firstLine && reqLen < REQUEST_MAXBUFFER-1){
      request[reqLen] = c;
      reqLen ++;
    } else if (headLen < HEADER_MAXBUFFER-1) {
      header[headLen] = c;
      headLen ++;
    }
    
    // Detect the end of the header
    if (c == '\n' && firstLine) {
      request[reqLen-1] = '\n';
      request[reqLen] = '\0';
      Serial.print(request);
      //Serial.println(F("### Ok Request ###"));
      val += _REQ_OK;
    } else if (c == '\n') {
      header[headLen-1] = '\n';
      header [headLen] = '\0';
      Serial.print(header);
      #ifdef ALLOW_CACHE
      if (strncmp(header, "If-None-Match", 5)==0){
        val += _NOT_MODIFIED;
      }
      #endif/* else if (strncmp(header, "Cache-Control", 5)==0){
        val += _NOT_MODIFIED;
      }*/
    }
    
    if (c == '\n' && currentLineIsBlank){
      return val;
    }
    
    // Detect line jumps
    if (c == '\n') {
      firstLine = false;
      currentLineIsBlank = true;
      reqLen = 0;
      headLen = 0;
    } else if (c != '\r') {
      currentLineIsBlank = false;
    }
  }
  return val;
}

/*****************************************************************************************************
 *                                          Configure Arduino                                        *
 *****************************************************************************************************/
void configure (char* code){
  byte nport, type, codelen;
  char inout;
  code ++;
  while(*code != '/')
    code ++;
  code ++;

  inout = code[0];
  nport = code[1] - 0x30;
  type = code[2];
  if (inout == _INPUT){
    if (nport > 5) return;
    conf.inputs[nport] = type;
    EEPROM.write(_EEPROM_BASE + nport, type);
  } else if (inout == _OUTPUT){
    if (nport > 5) return;
    conf.outputs[nport] = type;
    EEPROM.write(_EEPROM_BASE + nport + 6, type);
  } else if (inout == _NAME) {
    code ++;
    codelen = 0;
    for (byte i = 0; i<16&&code[i]!=' '; i++)
      codelen++;
    strncpy(conf.devName, code, codelen);
    conf.devName[codelen] = '\0';
    for (byte i = 0; i<codelen+1; i++){
      EEPROM.write(_EEPROM_BASE + 13 + i, conf.devName[i]);
    }
  } else if (inout == _ADDR){
    code ++;
    conf.ip = *code - 0x30;
    EEPROM.write(_EEPROM_BASE + 12, conf.ip);
  }
}

/*****************************************************************************************************
 *                                          SET Output                                               *
 *****************************************************************************************************/
void setOutput (char * cmd) {
  const byte outTable [6] = { 11, 10, 9, 6, 5, 3};
  cmd += 4;
  byte nport = *cmd - 0x30;
  if (nport > 5)
    return;
  cmd ++;
  switch(*cmd){
  case '0':
    digitalWrite(outTable[nport], LOW);
    break;
  case '1':
    digitalWrite(outTable[nport], HIGH);
    break;
  }
}

/*****************************************************************************************************
 *                                 ALL Sensors values Request (JSON)                                 *
 *****************************************************************************************************/
void portsRequested (){
  char baseRaw []= "_{\"val\":\"XXXX\"}_\0";
  char baseTemp []= "_{\"val\":\"+XXX.X ºC\"}_\0";
  char basePot []= "_{\"val\":\"XXX.X %\"}_\0";
  char baseLogical1 []= "_{\"val\":\"True\"}_\0";
  char baseLogical0 []= "_{\"val\":\"False\"}_\0";
  char baseEmpty []= "_{\"val\":\"Empty\"}_\0";
  char actBaseOn []=    "_{\"val\":\"On\"}_\0";
  char actBaseOff []=   "_{\"val\":\"Off\"}_\0";
  char actBaseEmpty []= "_{\"val\":\"Empty\"}_\0";
  const byte outTable [6] = { 11, 10, 9, 6, 5, 3 };
  client.print("{\"deviceName\":\"");
  client.print(conf.devName);
  client.print("\",\"inputs\":");
  for (char i = 0; i < 6; i++) {
    short raw = analogRead(i); // Read analog port value
    char type = conf.inputs[i];
    if (type == _RAW) {
        baseRaw[0] = (i==0)?'[':' ';
        baseRaw[12] = 0x30 + raw%10;
        raw /= 10;
        baseRaw[11] = 0x30 + raw%10;
        raw /= 10;
        baseRaw[10] = 0x30 + raw%10;
        baseRaw[9] = 0x30 + raw/10;
        baseRaw[15] = (i!=5) ? ',':']';
        client.print(baseRaw);
    } else if (type == _TEMPERATURE) {
        short temp = (raw*10)/4 - 205;
        baseTemp[0] = (i==0)?'[':' ';
        baseTemp[9] = (temp < 0)?'-':'+';
        baseTemp[14] = 0x30 + temp%10;
        temp /= 10;
        baseTemp[12] = 0x30 + temp%10;
        temp /= 10;
        baseTemp[11] = 0x30 + temp%10;
        baseTemp[10] = 0x30 + temp/10;
        baseTemp[21] = (i!=5)? ',':']';
        client.print(baseTemp);
    } else if (type == _POTENCIOMETER) {
      short percent = (((((raw*10)>>3)*10)>>3)*10)>>4;
      basePot[0] = (i==0)?'[':' ';
      basePot[13] = 0x30 + percent%10;
      percent /= 10;
      basePot[11] = 0x30 + percent%10;
      percent /= 10;
      basePot[10] = 0x30 + percent%10;
      basePot[9] = 0x30 + percent/10;
      basePot[18] = (i!=5)? ',':']';
      client.print(basePot);
    } else if (type == _LIGHT) {
        raw = raw >> 1;
        baseRaw[0] = (i==0)?'[':' ';
        baseRaw[12] = 0x30 + raw%10;
        raw /= 10;
        baseRaw[11] = 0x30 + raw%10;
        raw /= 10;
        baseRaw[10] = 0x30 + raw%10;
        baseRaw[9] = 0x30 + raw/10;
        baseRaw[15] = (i!=5) ? ',':']';
        client.print(baseRaw);
    } else if (type == _LOGICAL) {
      if (raw > 512){
        baseLogical1[0] = (i==0)?'[':' ';
        baseLogical1[15] = (i!=5)? ',':']';
        client.print(baseLogical1);
      } else {
        baseLogical0[0] = (i==0)?'[':' ';
        baseLogical0[16] = (i!=5)? ',':']';
        client.print(baseLogical0);
      }
    } else {
      baseEmpty[0] = (i==0)?'[':' ';
      baseEmpty[16] = (i!=5)? ',':']';
      client.print(baseEmpty);
    }
  }
  client.print(",\"outputs\":");
  for (char i = 0; i < 6; i++) {
    char type = conf.outputs[i];
    if (type == _LOGICAL){
      if (digitalRead(outTable[i])==1){
        actBaseOn[0] = (i==0)?'[':' ';
        actBaseOn[13] = (i!=5)? ',':']';
        client.print(actBaseOn);
      } else {
        actBaseOff[0] = (i==0)?'[':' ';
        actBaseOff[14] = (i!=5)? ',':']';
        client.print(actBaseOff);
      }
    } else {
      actBaseEmpty[0] = (i==0)?'[':' ';
      actBaseEmpty[16] = (i!=5)? ',':']';
      client.print(actBaseEmpty);
    }
  }
  client.print("}\n");
}

/*****************************************************************************************************
 *                                   Ports types request (JSON)                                      *
 *****************************************************************************************************/
void confRequest (){
  char confBase [] = "_{\"type\":\"X\"}_}\n\0";
  client.print("{\"deviceName\":\"");
  client.print(conf.devName);
  client.print("\",\"ip\":\"");
  client.print(conf.ip);
  client.print("\",\"ports\":");
  for (char i=0; i<12; i++){
    confBase [0] = (i==0)?'[':' ';
    confBase [13] = (i!=11)?',':']';
    confBase [14] = (i!=11)?'\0':'}';
    confBase[10] = conf.inputs[i];
    client.print(confBase);
  }
}

/*****************************************************************************************************
 *                                         Index.html request                                        *
 *****************************************************************************************************/
void fileRequest (const __FlashStringHelper* filePathP) {
  #define BUFFLEN (byte)128
  char  filePath[16];
  strcpy_P(filePath, (const prog_char*)filePathP);
  size_t i;
  uint8_t buffer [BUFFLEN+1];
  File file = SD.open(filePath);
  if (file) {
    // read from the file until there's nothing else in it:
    while (file.available()) {
      for (i = 0; i < BUFFLEN && file.available(); i++){
        buffer[i] = file.read();
      }
      buffer[i] = '\0';
      client.write(buffer, i);
    }
    // close the file:
    file.close();
  } else {
    Serial.print(F("File '"));
    Serial.print(filePath);
    Serial.println(F("' not found!"));
  }
}

/*****************************************************************************************************
 *                                          SETUP Subroutine                                         *
 *****************************************************************************************************/
void setup() {
  // Digital output vector
  const char outTable [6] = { 11, 10, 9, 6, 5, 3 };
  
  // Open serial communications and wait for port to open:
  Serial.begin(SERIAL_BAUDRATE);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for Leonardo only
  }
  Serial.println(F("--- Arduino Ethernet Debug interface ---"));

  // Setup ports configuration
  byte* confptr = (byte*)&conf;
  for (byte i=0; i<sizeof(Configuration); i++){
    confptr[i] = EEPROM.read(_EEPROM_BASE + i);
  }

  /* Uncomment next line to reset network Addr */
  //configure("//a0");

  // start the Ethernet connection and the server:
  byte ipAddr [4];
  byte gwAddr [4];
  byte subnet [4];
  byte dns[4] = {147, 83, 2, 3};
  
    if (conf.ip == 0){ /* --- NETWORK CONFIGURATION 0 --- */
    // Ip Address 192.168.10.2
    ipAddr[0] = 192;
    ipAddr[1] = 168;
    ipAddr[2] = 10;
    ipAddr[3] = 2;
    // Gateway Address 192.168.10.1
    gwAddr[0] = 192;
    gwAddr[1] = 168;
    gwAddr[2] = 10;
    gwAddr[3] = 1;
    // Subnet Mask 255.255.255.0 (/24)
    subnet[0] = 255;
    subnet[1] = 255;
    subnet[2] = 255;
    subnet[3] = 0;
  } else if (conf.ip==1){ /* --- NETWORK CONFIGURATION 1 --- */
    // Ip Address 192.168.10.130
    ipAddr[0] = 192;
    ipAddr[1] = 168;
    ipAddr[2] = 10;
    ipAddr[3] = 130;
    // Gateway Address 192.168.10.1
    gwAddr[0] = 192;
    gwAddr[1] = 168;
    gwAddr[2] = 10;
    gwAddr[3] = 129;
    // Subnet Mask 255.255.255.192 (/26)
    subnet[0] = 255;
    subnet[1] = 255;
    subnet[2] = 255;
    subnet[3] = 192;
  } else if (conf.ip==2){
    // Ip Address 10.0.1.2
    ipAddr[0] = 10;
    ipAddr[1] = 0;
    ipAddr[2] = 1;
    ipAddr[3] = 2;
    // Gateway Address 10.0.1.1
    gwAddr[0] = 10;
    gwAddr[1] = 0;
    gwAddr[2] = 1;
    gwAddr[3] = 1;
    // Subnet Mask 255.255.255.192 (/24)
    subnet[0] = 255;
    subnet[1] = 255;
    subnet[2] = 255;
    subnet[3] = 0;
  } else if (conf.ip==3){
    // Ip Address 10.0.1.130
    ipAddr[0] = 10;
    ipAddr[1] = 0;
    ipAddr[2] = 1;
    ipAddr[3] = 130;
    // Gateway Address 10.0.1.129
    gwAddr[0] = 10;
    gwAddr[1] = 0;
    gwAddr[2] = 1;
    gwAddr[3] = 129;
    // Subnet Mask 255.255.255.192 (/26)
    subnet[0] = 255;
    subnet[1] = 255;
    subnet[2] = 255;
    subnet[3] = 192;
  } 
  // Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network:
  byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
  IPAddress ip (ipAddr[0],ipAddr[1],ipAddr[2],ipAddr[3]);
  Ethernet.begin(mac, ip);
  

  
  server.begin();
  Serial.print(F("Server is at "));
  Serial.print(Ethernet.localIP());
  Serial.print(F(":"));
  Serial.println(SERVER_WEBPORT);
  
  // Setup outputs
  for (byte i = 0; i < 6; i++){
    pinMode(outTable[i], OUTPUT);
  }
  
  // Create SD
  digitalWrite(10, LOW);
  digitalWrite(10, HIGH);
  if (!SD.begin(4)) {
    Serial.println(F("SD Initialization failed!"));
    while(true)delay(1000);
  }
}

void loop() {
  const __FlashStringHelper* jsonHeadPath = F("/JSONHEAD.HDR");
  const __FlashStringHelper* htmlHeadPath = F("/HTMLHEAD.HDR");
  const __FlashStringHelper* jsHeadPath = F("/JSHEAD.HDR");
  const __FlashStringHelper* cssHeadPath = F("/CSSHEAD.HDR");

  // Listen for incoming clients
  client = server.available();
  char* path = request;

  if (!client)
    return;
    
  Serial.println(F("--- Begin of the Request ---"));
  byte headers = requestRead(client);
  if ((headers & _REQ_OK) == 0)
    return;
  
  while(*path != '/')
    path++;
  path++;
      
  if (strncmp(request, "GET", 3)!=0)
    return;

  if ((headers & _NOT_MODIFIED) != 0){
      Serial.println(F("Sending not modified"));
      client.println("HTTP/1.1 304 - Not Modified");
  } else if (strncmp(path, "ports", 5) == 0) {
    fileRequest(jsonHeadPath);
    portsRequested();
  } else if (strncmp(path, "config", 6) == 0){
    fileRequest(jsonHeadPath);
    confRequest();
  } else if (strncmp(path, "set", 3)==0){
    configure(path);
    fileRequest(jsonHeadPath);
    confRequest();
  } else if (strncmp(path, "out",3)==0){
    setOutput(path);
    fileRequest(jsonHeadPath);
    portsRequested();
  } else if (strncmp(path, "index", 5) == 0){
    fileRequest(htmlHeadPath);
    fileRequest(F("/INDEX.TXT"));
  } else if (strncmp(path, "bs.css", 6) == 0){
    fileRequest(cssHeadPath);
    fileRequest(F("/BS.CSS"));
  } else if (strncmp(path, "bs-res.css", 10) == 0){
    fileRequest(cssHeadPath);
    fileRequest(F("/BSR.CSS"));
  } else if (strncmp(path, "jquery.js", 9) == 0){
    fileRequest(jsHeadPath);
    fileRequest(F("/JQUERY.TXT"));
  } else if (strncmp(path, "control.js", 10) == 0){
    fileRequest(jsHeadPath);
    fileRequest(F("/CTRL.TXT"));
  } else if (strncmp(path, "bs.js", 5) == 0){
    fileRequest(jsHeadPath);
    fileRequest(F("/BSJS.TXT"));
  } else if (strncmp(path, "shield.jpg", 5) == 0){
    fileRequest(F("/SHIELD.TXT"));
  } else if (*path == ' '){
    fileRequest(htmlHeadPath);
    fileRequest(F("/INDEX.TXT"));
  } else {
    Serial.println(F("### Bad Path request ###"));
    client.print("HTTP/1.1 404 - File not Found\n");
  }
  Serial.println(F("---  End of the Request  ---"));

  // close the connection:
  if (client){
    client.stop();
  }
}

