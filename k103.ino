#include <EEPROM.h>

#define BOLEX_SWITCH  12 // pulls down
#define BOLEX_SHUTTER 8  // active low
#define K103_ADVANCE  4  // active high "drive"
#define K103_REVERSE  7  // active low
#define K103_TAKEUP   9  // active high???
#define K103_FWD_SW   2  // pulls down
#define K103_REV_SW   3  // pulls down

#define K103_FRAME_TIME 1200  // ms
#define BOLEX_FRAME_TIME 800  // ms

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

struct Reel {
  unsigned long ts;
  char desc[20];
  long len;
  long frame;
};

Reel bolex;
Reel k103;

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
  Serial.println("hello");

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
    delay(BOLEX_FRAME_TIME);
  }
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
    delay(K103_FRAME_TIME);
  }
  digitalWrite(K103_TAKEUP, LOW);
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
    delay(K103_FRAME_TIME);
  }
  digitalWrite(K103_TAKEUP, LOW);
}

void handle_load_reel(Reel * r, uint16_t eep_offset) {
  // *    DC1 '!' 'C|P'N ts(4) desc(20) len(4)
  unsigned long ts;
  char desc[20];
  long len, frame;
  getSerial(ts);
  getSerial(desc);
  getSerial(len);
  getSerial(frame);
  load_film(r, eep_offset, ts, desc, len, frame);
}

template< typename T > T &getSerial(T &t) {
  Serial.readBytes((uint8_t*)&t, sizeof(T));
  return t;
}

template < typename T > T &putSerial(T &t) {
  Serial.write((uint8_t*)&t, sizeof(T));
  return t;
}

void handle_reel_command() {
  // TODO: timeout or other escape
  byte c;
  getSerial(c);
  char device;
  switch (c) {
    case '?':
      getSerial(device);
      if (device == 'C') {
        putSerial(bolex);
      } else {
        Serial.println("Not yet implemented");
      }
      return;
    case '!':
      getSerial(device);
      if (device == 'C') {
        handle_load_reel(&bolex, EEP_BOLEX_OFFSET);
      } else {
        Serial.println("Not yet implemented");
      }
      return;
    default: return Serial.println("not yet implemented");
  }
}

void handle_frame_command() {
  // TODO: timeout or other escape
  byte c;
  getSerial(c);
  uint8_t n;
  switch (c) {
    case '*':
      getSerial(n);
      return capture(&bolex, n);
    case 'F':
      getSerial(n);
      return forward(&k103, n);
    case 'R':
      getSerial(n);
      return reverse(&k103, n);
    case '?': return Serial.println("not yet implemented");
    default:
      Serial.print("bad frame command byte: ");
      Serial.println(c, HEX);
  }
}

void handle_serial() {
  if (Serial.available() > 0) {
    byte c;
    getSerial(c);
    switch (c) {
      case ASCII_DC1: return handle_reel_command();
      case ASCII_DC2: return handle_frame_command();
      default:
        Serial.print("bad command byte: 0x");
        Serial.println(c, HEX);
    }
  }
}

void loop() {
  handle_serial();
}

