#include <USBComposite.h>
#include <stdarg.h>
#include "CEClient.h"
#include <ctype.h>

#define memzero(p,n) memset((p),0,(n))

#define DEVICE_TYPE                  CEC_LogicalDevice::CDT_PLAYBACK_DEVICE //CEC_LogicalDevice::CDT_RECORDING_DEVICE

#define EEPROM_MODE                  0
#define EEPROM_PHYSICAL_ADDRESS_HIGH 1
#define EEPROM_PHYSICAL_ADDRESS_LOW  2
#define EEPROM_DEVICE                3
#define EEPROM_MONITOR               4
#define EEPROM_PROMISCUOUS           5

#define CEC_PHYSICAL_ADDRESS    0x1000
#define CEC_INPUT_PIN           PA0
#define CEC_OUTPUT_PIN          -1

uint16_t physicalAddress = CEC_PHYSICAL_ADDRESS;
uint8_t deviceType = DEVICE_TYPE;
USBHID HID;
HIDKeyboard Keyboard(HID);
HIDConsumer Consumer(HID);
USBCompositeSerial CompositeSerial;

#define MAX_COMMAND 256
char commandLine[MAX_COMMAND+1];
unsigned commandLineLength = 0;

const uint8_t reportDescription[] = {
   HID_KEYBOARD_REPORT_DESCRIPTOR(),
   HID_CONSUMER_REPORT_DESCRIPTOR(),
};

enum {
  MODE_KEYBOARD = 0,
  MODE_CONSUMER = 1,
  MODE_SERIAL = 2,
  MODE_UNDEFINED = 0xFF
};

uint8 mode = MODE_KEYBOARD;
uint8 monitor = 0;
uint8 promiscuous = 0;

const struct {
  uint8_t cec;
  uint16_t keyboard;
  uint16_t consumer;
} dict[] = {
  {0,KEY_RETURN,HIDConsumer::MENU_PICK},
  {1,KEY_UP_ARROW,HIDConsumer::MENU_UP},
  {2,KEY_DOWN_ARROW,HIDConsumer::MENU_DOWN},
  {3,KEY_LEFT_ARROW,HIDConsumer::MENU_LEFT},
  {4,KEY_RIGHT_ARROW,HIDConsumer::MENU_RIGHT}, 
  {0xD,KEY_BACKSPACE,HIDConsumer::MENU_ESCAPE},
  {0xB,KEY_HID_OFFSET+0x76,HIDConsumer::MENU},
  {0x48,KEY_PAGE_UP,HIDConsumer::REWIND},
  {0x49,KEY_PAGE_DOWN,HIDConsumer::FAST_FORWARD},
  {0x46,' ',HIDConsumer::PLAY_OR_PAUSE}
};

#define DICT_SIZE (sizeof dict / sizeof *dict)

class QuietCEClient : public CEClient {
public:  
  void OnReady() {}
  QuietCEClient (int physicalAddress, int inputPin, int outputPin) : CEClient(physicalAddress,inputPin,outputPin) {}
};

// create a CEC client
QuietCEClient ceclient(CEC_PHYSICAL_ADDRESS, CEC_INPUT_PIN, CEC_OUTPUT_PIN);

void MyDbgPrint(const char* fmt, ...)
{
        char FormatBuffer[128]; 
        va_list args;
        va_start(args, fmt);
        vsprintf(FormatBuffer, fmt, args);
       
        char c;
        char* addr = FormatBuffer;
      
        while ((c = *addr++))
        {
          CompositeSerial.print(c);
        }
}

void receiver(int source, int dest, unsigned char* buffer, int count) {
  MyDbgPrint("Packet received at %ld: %02d -> %02d: %02X", millis(), source, dest, ((source&0x0f)<<4)|(dest&0x0f));
  for (int i = 0; i < count; i++)
    MyDbgPrint(":%02X", buffer[i]);
  MyDbgPrint("\n");
  if (count == 2 && buffer[0] == 0x44) {
      for (unsigned i=0; i<DICT_SIZE; i++)
        if (dict[i].cec == buffer[1]) {
          if (mode == MODE_KEYBOARD) 
            Keyboard.press(dict[i].keyboard);
          else if (mode == MODE_CONSUMER)
            Consumer.press(dict[i].consumer);
        }
  }
  else if (count >= 1 && buffer[0] == 0x45) {
    if (mode == MODE_KEYBOARD) {
      Keyboard.releaseAll();
    }
    else if (mode == MODE_CONSUMER) {
      Consumer.release();
    }
  }
}

void setup() {
    mode = EEPROM8_getValue(EEPROM_MODE);
    deviceType = EEPROM8_checkValue(EEPROM_DEVICE) ? EEPROM8_getValue(EEPROM_DEVICE) : (uint8)DEVICE_TYPE;
    monitor = EEPROM8_getValue(EEPROM_MONITOR);
    promiscuous = EEPROM8_getValue(EEPROM_PROMISCUOUS);
    if (EEPROM8_checkValue(EEPROM_PHYSICAL_ADDRESS_HIGH) && EEPROM8_checkValue(EEPROM_PHYSICAL_ADDRESS_LOW)) {
      physicalAddress = ((uint16_t)EEPROM8_getValue(EEPROM_PHYSICAL_ADDRESS_HIGH) << 8)|EEPROM8_getValue(EEPROM_PHYSICAL_ADDRESS_HIGH);
    }
    ceclient.setPhysicalAddress(physicalAddress);
    ceclient.setPromiscuous(promiscuous||monitor);
    ceclient.setMonitorMode(monitor);

    HID.begin(CompositeSerial, reportDescription, sizeof(reportDescription));
    
    while (!USBComposite);
    
    Keyboard.begin(); // needed for LED support, which we may not care about
    
    ceclient.begin((CEC_LogicalDevice::CEC_DEVICE_TYPE)deviceType);
    ceclient.onReceiveCallback(receiver);
    CompositeSerial.println("CEC Client started");
}

void switchMode(uint8 newMode) {
  if (newMode == mode)
    return;
  if (mode == MODE_KEYBOARD)
    Keyboard.releaseAll();
  else if (mode == MODE_CONSUMER)
    Consumer.release();
  EEPROM8_storeValue(EEPROM_MODE, newMode);
  mode = newMode;
} 

uint8* parseHexData(char* command, unsigned* lengthP) {
  static uint8 buffer[24];
  unsigned inBuffer = 0;
  unsigned bitPos = 0;
  memzero(buffer, 24);
  while (*command && isspace(*command)) 
    command++;
  while (*command) {
    uint16 nibble = 0xFFFF;
    uint8 c = *command;
    if (!c)
      break;
    if (c == ':' || isspace(c)) {
      if (bitPos) {
        inBuffer++;
        bitPos = 0;
      }
    }
    else if ('0' <= c && c <= '9') {
      nibble = c - '0';
    }
    else if ('a' <= c && c <= 'f') {
      nibble = c - 'a' + 10;
    }
    else if ('A' <= c && c <= 'F') {
      nibble = c - 'A' + 10;
    }
    
    if (nibble != 0xFFFF) {
      if (bitPos) 
         buffer[inBuffer] <<= 4;
      if (inBuffer >= 24)
          return NULL;
      buffer[inBuffer] |= nibble;
      
      if (bitPos) {
        inBuffer++;
        bitPos = 0;
      }
      else {
        bitPos = 4;
      }
    }
    command++;
  }
  if (bitPos)
      inBuffer++;
  if (inBuffer > 24)
    inBuffer = 24;
  *lengthP = inBuffer;
  return buffer;
}

void showNibble(uint8_t nibble) {
  nibble &= 0xF;
  if (nibble < 10)
    Serial.write('0'+nibble);
  else
    Serial.write('a'-10+nibble);
}

void processCommand(char* command) {
  if (!strncmp(command,"mode ",5)) {
    command += 5;
    if (!strcmp(command,"keyboard")) {
      switchMode(MODE_KEYBOARD);
      CompositeSerial.println("ok mode keyboard");
    }
    else if (!strcmp(command,"consumer")) {
      switchMode(MODE_CONSUMER);
      CompositeSerial.println("ok mode consumer");
    }
    else if (!strcmp(command,"serial")) {
      switchMode(MODE_SERIAL);
      CompositeSerial.println("ok mode serial");
    }
  }
  else if (!strncmp(command,"tx ",3)) {
    unsigned n;
    uint8* buffer = parseHexData(command+3,&n);
    if (n > 0) {
      ceclient.TransmitFrame(buffer[0] & 0xF, buffer+1, n-1);
    }
    else {
      CompositeSerial.println("input error");
    }
  }
  else if (!strncmp(command,"monitor ",8)) {
    unsigned n;
    uint8* buffer = parseHexData(command+8,&n);
    if (n == 1) {
      monitor = !!buffer[0];
      EEPROM8_storeValue(EEPROM_MONITOR, monitor);
    }
    else {
      CompositeSerial.println("input error");
    }
  }
  else if (!strncmp(command,"promiscuous ",12)) {
    unsigned n;
    uint8* buffer = parseHexData(command+12,&n);
    if (n == 1) {
      promiscuous = !!buffer[0];
      EEPROM8_storeValue(EEPROM_MONITOR, monitor);
    }
    else {
      CompositeSerial.println("input error");
    }
  }
  else if (!strncmp(command,"physical ", 9)) {
    unsigned n;
    uint8* buffer = parseHexData(command+9,&n);
    if (n == 2) {
      physicalAddress = ((uint16_t)buffer[0] << 8) | buffer[1];
      EEPROM8_storeValue(EEPROM_PHYSICAL_ADDRESS_HIGH, physicalAddress >> 8);
      EEPROM8_storeValue(EEPROM_PHYSICAL_ADDRESS_LOW, physicalAddress & 0xFF);
    }
    else {
      CompositeSerial.println("input error");
    }
  }
  else if (!strncmp(command,"device ", 7)) {
    unsigned n;
    uint8* buffer = parseHexData(command+7,&n);
    if (n == 1) {
      deviceType = buffer[0];
      EEPROM8_storeValue(EEPROM_DEVICE, deviceType);
    }
    else {
      CompositeSerial.println("input error");
    }
  }
  else if (!strcmp(command,"init")) {
      ceclient.setPromiscuous(promiscuous||monitor);
      ceclient.setMonitorMode(monitor);
      ceclient.setPhysicalAddress(physicalAddress);
      ceclient.Initialize((CEC_LogicalDevice::CEC_DEVICE_TYPE)deviceType);
  }
  else if (!strcmp(command, "show")) {
      CompositeSerial.print("mode ");
      switch(mode) {
        case MODE_KEYBOARD:
          CompositeSerial.println("keyboard");
          break;
        case MODE_SERIAL:
          CompositeSerial.println("serial");
          break;
        case MODE_CONSUMER:
          CompositeSerial.println("consumer");
          break;
      }
      CompositeSerial.print("physical ");
      showNibble(physicalAddress >> 12);
      CompositeSerial.write('.');
      showNibble(physicalAddress >> 8);
      CompositeSerial.write('.');
      showNibble(physicalAddress >> 4);
      CompositeSerial.write('.');
      showNibble(physicalAddress);
      CompositeSerial.write('\n');
      CompositeSerial.print("monitor ");
      showNibble(monitor);
      CompositeSerial.print("\npromiscuous ");
      showNibble(promiscuous);
      CompositeSerial.print("\ndevice ");
      showNibble(deviceType);
      CompositeSerial.print("\nlogical ");
      showNibble(ceclient.getLogicalAddress());
      CompositeSerial.println("\ndone");
  }
  else if (!strcmp(command, "help")) {
      CompositeSerial.println("tx destination nn:nn:nn:nn:...");
      CompositeSerial.println("mode [keyboard|consumer|serial]");
      CompositeSerial.println("physical a.b.c.d");
      CompositeSerial.println("monitor 0|1");
      CompositeSerial.println("promiscuous 0|1");
      CompositeSerial.println("device n");
      CompositeSerial.println("show");
      CompositeSerial.println("init");
  }
  else {
      CompositeSerial.println("input error");
  }
}

void loop() {
    ceclient.run();
    while(CompositeSerial.available()) {
      char c = CompositeSerial.read();
      if (c == '\r' || c == '\n') {
        if (commandLineLength > 0) {
          if (commandLineLength <= MAX_COMMAND) {
            commandLine[commandLineLength] = 0;
            processCommand(commandLine);
          }
          commandLineLength = 0;
        }
      }
      else {
        if (commandLineLength < MAX_COMMAND) {
          commandLine[commandLineLength++] = c;
        }
        else {
          commandLineLength = MAX_COMMAND + 1;
        }
      }
    }
}

