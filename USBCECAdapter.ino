/*
    Example1_Listening.ino

    Basic example to demonstrate the use of the CEClient library
    The client is configured in promiscuous and monitor mode 
    to receive all the messages on the CEC bus
    
    No specific callback function is defined, therefore the client
    calls the default one, which prints the packets on the Serial port

    Use http://www.cec-o-matic.com/ to decode captured messages
*/

#include <USBComposite.h>
#include <stdarg.h>
#include "CEClient.h"
#include <ctype.h>

#define EEPROM_MODE                  0
#define EEPROM_PHYSICAL_ADDRESS_HIGH 1
#define EEPROM_PHYSICAL_ADDRESS_LOW  2

#define CEC_PHYSICAL_ADDRESS    0x1000
#define CEC_INPUT_PIN           PA0
#define CEC_OUTPUT_PIN          -1

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
      for (int i=0; i<DICT_SIZE; i++)
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
    HID.begin(CompositeSerial, reportDescription, sizeof(reportDescription));
    while (!USBComposite);
    Keyboard.begin(); // needed for LED support, which we may not care about
    ceclient.begin(CEC_LogicalDevice::CDT_RECORDING_DEVICE);
    ceclient.setPromiscuous(false);
    ceclient.setMonitorMode(false);
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

void processCommand(char* command) {
  if (!strncmp(command,"mode ",5)) {
    command += 5;
    if (!strcmp(command,"keyboard")) {
      switchMode(MODE_KEYBOARD);
    }
    else if (!strcmp(command,"consumer")) {
      switchMode(MODE_CONSUMER);
    }
    else if (!strcmp(command,"serial")) {
      switchMode(MODE_SERIAL);
    }
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

