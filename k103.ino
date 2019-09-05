#include <EEPROM.h>
#include "packetizer.h"

#define BOLEX_SWITCH  12 // pulls down
#define BOLEX_SHUTTER 8  // active low
#define K103_ADVANCE  4  // active high "drive"
#define K103_REVERSE  7  // active low
#define K103_TAKEUP   9  // active high???
#define K103_FWD_SW   2  // pulls down
#define K103_REV_SW   3  // pulls down

#define K103_FRAME_TIME 1200  // ms
#define BOLEX_FRAME_TIME 800  // ms
#define FRAME_UPDATE_THROTTLE 60000  // ms, save eeprom wear for higher data loss risk


/**
 * K-103 control system
 * rewired for AC isolation and a bit of automation by phil for madi, 2019
 * 
 * the main aluminum control box has responsibilities:
 * - all hardware IO
 *    -> exposed as basic functions, so that we can do
 * - frame counting (for both k103 and bolex)
 *    -> should be persisted to eeprom
 * - a nice command-oriented serial interface
 * 
 * the main control box is specifically not responsible for
 * - automation
 * - a directly-usable interface
 * 
 * command set:
 * 
 * Reels (DC1)
 * - load film
 *    DC1 '!' 'C|P'N ts(4) desc(8) len(4)
 *    c|k => camera | projector
 *    N => always 0
 * 
 * - get film
 *    DC1 '?' 'C|P'N
 * 
 * Frames (DC2)
 * - bolex: capture frames
 *    DC2 '*' N
 * 
 * - k103: advance frames
 *    DC2 'F' N
 * 
 * - k103: reverse frames
 *    DC2 'R' N
 * 
 * - get frame number
 *    DC2 '?'
 * 
 * EEPROM shape
 * 
 * Reel: 32 bytes
 * off  size  desc            type
 *   0   4    load timestamp  unsigned long (NB: year 2106)
 * + 4  20    description     char[8]
 * +24   4    reel length     long
 * +28   4    current frame   long
 * 
 */

#define EEP_BOLEX_OFFSET   0
#define EEP_K103_OFFSET   40

#define ASCII_DC1 0x11
#define ASCII_DC2 0x12


// reels
#define CMD_LOAD_BOLEX 0x00
#define CMD_LOAD_K103  0x01
#define CMD_GET_BOLEX  0x08
#define CMD_GET_K103   0x09

// frames
#define CMD_FWD_BOLEX  0x10
#define CMD_FWD_K103   0x11
#define CMD_REV_BOLEX  0x18
#define CMD_REV_K103   0x19


//template< typename T > T &getFromFrame(Framer &f, uint8_t len, T &t) {
////  assert(
//  Serial.readBytes((uint8_t*)&t, sizeof(T));
//  return t;
//}
//
//template < typename T > T &putToFrame(Framer &f, T &t) {
//  Serial.write((uint8_t*)&t, sizeof(T));
//  return t;
//}


//void Framer::handle_command(byte command, byte * data, uint8_t data_length) {
//  switch (command) {
//  case RX_ECHO:
//    write(TX_LOG_FRAMER, "echo:", 5);
//    write(TX_LOG_FRAMER, data, data_length);
//    break;
//  case CMD_LOAD_BOLEX:
//    print("suuuuuup");
//    break;
//  default:
//    print(TX_LOG_FRAMER, "unrecognized command:");
//    print(String(command, HEX));
//  }
//}
//
//Framer f = Framer(&Serial);


Packetizer packetizer = Packetizer(&Serial);


struct Reel {
  unsigned long ts;
  char desc[20];
  long len;
  long frame;
};

Reel bolex;
Reel k103;
boolean bolex_frame_dirty = false;
boolean k103_frame_dirty = false;
unsigned long bolex_last_frame_save = 0 - FRAME_UPDATE_THROTTLE;
unsigned long k103_last_frame_save = 0 - FRAME_UPDATE_THROTTLE;

void update_frame(Reel * r, int n) {
  r->frame += n;
  if (r == &bolex) {
    bolex_frame_dirty = true;
  } else if (r == &k103) {
    k103_frame_dirty = true;
  } else {
    Serial.println("invlalid reel specified for frame update");
  }
}

bool _persist_reel_frame(unsigned long now, Reel * r, boolean * dirty,
                         unsigned long * last_frame_save, uint16_t eep_offset,
                         boolean ignore_throttle) {
  boolean throttle_cleared = (now - *last_frame_save) > FRAME_UPDATE_THROTTLE;
  if (*dirty && (throttle_cleared || ignore_throttle)) {
    EEPROM.put(eep_offset + 28, r->frame);
    *dirty = false;
    *last_frame_save = now;
  }
  return *dirty;
}
bool persist_frames(boolean ignore_throttle=false) {
  unsigned long now = millis();
  _persist_reel_frame(now, &bolex, &bolex_frame_dirty, &bolex_last_frame_save, EEP_BOLEX_OFFSET, ignore_throttle);
  _persist_reel_frame(now, &k103, &k103_frame_dirty, &k103_last_frame_save, EEP_K103_OFFSET, ignore_throttle);
}

void restore_reel_state(Reel * r, uint16_t eep_offset) {
  EEPROM.get(eep_offset, *r);
}

void load_film(Reel * r, uint16_t eep_offset, unsigned long ts, char desc[20], long len, long frame=0) {
  r->ts = ts;
  strncpy(r->desc, desc, 20);
  r->len = len;
  r->frame = frame;
  EEPROM.put(eep_offset, *r);
}

void setup() {
  pinMode(BOLEX_SWITCH, INPUT_PULLUP);
  pinMode(BOLEX_SHUTTER, OUTPUT);
  digitalWrite(BOLEX_SHUTTER, HIGH);  // active-low

  pinMode(K103_ADVANCE, OUTPUT);
  pinMode(K103_REVERSE, OUTPUT);
  digitalWrite(K103_REVERSE, HIGH);  // active-low
  pinMode(K103_TAKEUP, OUTPUT);
  pinMode(K103_FWD_SW, INPUT_PULLUP);
  pinMode(K103_REV_SW, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(K103_FWD_SW), fwd_isr, FALLING);
  attachInterrupt(digitalPinToInterrupt(K103_REV_SW), rev_isr, FALLING);

  Serial.begin(9600);
//  Serial.println("hello");

  restore_reel_state(&bolex, EEP_BOLEX_OFFSET);
  restore_reel_state(&k103, EEP_K103_OFFSET);

//  TCCR1B = TCCR1B & B11111000 | B00000010; // timer1 PWM frequency 3921.16 Hz
}

void fwd_isr() {
}
void rev_isr() {
}

void capture(Reel * r, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    digitalWrite(BOLEX_SHUTTER, LOW);
    delay(60);
    digitalWrite(BOLEX_SHUTTER, HIGH);
    update_frame(r, 1);
    delay(BOLEX_FRAME_TIME);
  }
  persist_frames(true);
}

void forward(Reel * r, uint8_t n) {
  if (digitalRead(K103_REVERSE) == LOW) {
    digitalWrite(K103_REVERSE, HIGH);
    delay(30);
  }
  analogWrite(K103_TAKEUP, 127);
  for (uint8_t i = 0; i < n; i++) {
    digitalWrite(K103_ADVANCE, HIGH);
    delay(30);
    digitalWrite(K103_ADVANCE, LOW);
    update_frame(r, 1);
    delay(K103_FRAME_TIME);
  }
  persist_frames(true);
  digitalWrite(K103_TAKEUP, LOW);
  Serial.print("advanced frames: ");
  Serial.println(n);
}

void reverse(Reel * r, uint8_t n) {
  if (digitalRead(K103_REVERSE) == HIGH) {
    digitalWrite(K103_REVERSE, LOW);
    delay(30);
  }
  analogWrite(K103_TAKEUP, 127);
  for (uint8_t i = 0; i < n; i++) {
    digitalWrite(K103_ADVANCE, HIGH);
    delay(30);
    digitalWrite(K103_ADVANCE, LOW);
    update_frame(r, -1);
    delay(K103_FRAME_TIME);
  }
  persist_frames(true);
  digitalWrite(K103_TAKEUP, LOW);
}

void handle_load_reel(Reel * r, uint16_t eep_offset) {
  // *    DC1 '!' 'C|P'N ts(4) desc(20) len(4)
  unsigned long ts;
  char desc[20];
  long len, frame;
//  getSerial(ts);
//  getSerial(desc);
//  getSerial(len);
//  getSerial(frame);
  load_film(r, eep_offset, ts, desc, len, frame);
}

void handle_reel_command() {
//  // TODO: timeout or other escape
//  byte c;
//  getSerial(c);
//  char device;
//  switch (c) {
//    case '?':
//      getSerial(device);
//      if (device == 'C') {
//        putSerial(bolex);
//      } else if (device == 'P') {
//        putSerial(k103);
//      } else {
//        Serial.println("Not yet implemented");
//      }
//      return;
//    case '!':
//      getSerial(device);
//      if (device == 'C') {
//        handle_load_reel(&bolex, EEP_BOLEX_OFFSET);
//      } else if (device == 'P') {
//        handle_load_reel(&k103, EEP_K103_OFFSET);
//      } else {
//        Serial.println("Not yet implemented");
//      }
//      return;
//    default: return Serial.println("not yet implemented");
//  }
}

void handle_serial() {
//  byte c;
//  if (Serial.available() > 0) {
////    getSerial(c);
//    switch (c) {
//      case ASCII_DC1: return handle_reel_command();
//      case ASCII_DC2: return handle_frame_command();
//      default:
//        Serial.print("bad command byte: 0x");
//        Serial.println(c, HEX);
//    }
//  }
}

void get_packet() {
  byte rec[62];
  uint8_t len;
  packetizer.receive(rec, &len);
  switch (rec[0]) {
  case ASCII_DC1:  // reel command
    switch(rec[1]) {
    case '?':
      if (rec[2] == 'C') {
        packetizer.put(bolex);
      } else if (rec[2] == 'P') {
        packetizer.put(k103);
      } else {
        packetizer.debug("expected 'C' or 'P' for '?'");
        packetizer.debug(rec[2], 1);
      }
      return;
    case '!':
      if (rec[2] == 'C') {
        handle_load_reel(&bolex, EEP_BOLEX_OFFSET);
      } else if (rec[2] == 'P') {
        handle_load_reel(&k103, EEP_K103_OFFSET);
      } else {
        packetizer.debug("expected 'C' or 'P' for '!'");
        packetizer.debug(rec[2], 1);
      }
      return;
    default:
      packetizer.debug("bad reel command byte");
      packetizer.debug(String(rec[0], DEC));
    }
    break;
  case ASCII_DC2:  // frame command
    switch(rec[1]) {
    case '*':
      return capture(&bolex, rec[2]);
    case 'F':
      return forward(&k103, rec[2]);
    case 'R':
      return reverse(&k103, rec[2]);
    case '?':
      return packetizer.debug("'?' not yet implemented");
    default:
      packetizer.debug("bad frame command byte");
      packetizer.debug(String(rec[0], DEC));
    }
    break;
  default:
    packetizer.debug("bad command byte");
    packetizer.debug(String(rec[0], DEC));
  }
}



void loop() {
  if (packetizer.might_have_something()) {
    get_packet();
  }
//  f.poll();
//  handle_serial();
//  persist_frames();
}

