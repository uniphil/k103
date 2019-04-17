#include <EEPROM.h>

#define BOLEX_SWITCH  12 // pulls down
#define BOLEX_SHUTTER 8  // active low
#define K103_ADVANCE  4  // active high "drive"
#define K103_REVERSE  7  // active low
#define K103_TAKEUP   9  // active high???
#define K103_FWD_SW   2  // pulls down
#define K103_REV_SW   3  // pulls down

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
 * Reel: 20 bytes
 * off  size  desc            type
 *   0  4     load timestamp  unsigned long (NB: year 2106)
 * + 4  8     description     char[8]
 * +12  4     reel length     long
 * +16  4     current frame   long
 * 
 */

#define EEP_BOLEX_OFFSET   0
#define EEP_K103_OFFSET   40
// 20-byte extra gap: can add features if needed without having to realign
// or even, add a second camera?

#define ASCII_DC1 0x11
#define ASCII_DC2 0x12

struct Reel {
  uint16_t offset;
  unsigned long ts;
  char desc[8];
  long len;
  long frame;
};

Reel bolex;
Reel k103;

void restore_reel_state(Reel * r, uint16_t eep_offset) {
  r->offset = eep_offset;
  EEPROM.get(eep_offset + 0, r->ts);
  EEPROM.get(eep_offset + 4, r->desc);
  EEPROM.get(eep_offset + 12, r->len);
  EEPROM.get(eep_offset + 16, r->frame);
}

void load_film(Reel * r, unsigned long ts, char desc[8], long len, long frame=0) {
  r->ts = ts;
  strncpy(desc, r->desc, 8);
  r->len = len;
  r->frame = frame;

  uint16_t eep_offset = r->offset;
  EEPROM.put(eep_offset + 0, r->ts);
  EEPROM.put(eep_offset + 4, r->desc);
  EEPROM.put(eep_offset + 12, r->len);
  EEPROM.put(eep_offset + 16, r->frame);
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

  restore_reel_state(&bolex, EEP_BOLEX_OFFSET);
  restore_reel_state(&k103, EEP_K103_OFFSET);

  Serial.begin(9600);
  Serial.println("hello");
  Serial.println(bolex.ts);
  Serial.println(bolex.desc);
  Serial.println(bolex.len);
  Serial.println(bolex.frame);

//  TCCR1B = TCCR1B & B11111000 | B00000010; // timer1 PWM frequency 3921.16 Hz
}

void fwd_isr() {
}
void rev_isr() {
}

void forward(Reel * r, uint8_t n) {
  digitalWrite(K103_REVERSE, HIGH);
  // TODO: only wait if we're actually flipping it
  delay(30);
  analogWrite(K103_TAKEUP, 127);
  digitalWrite(K103_ADVANCE, HIGH);
  delay(30);
  digitalWrite(K103_ADVANCE, LOW);
  delay(1200);
  digitalWrite(K103_TAKEUP, LOW);
  Serial.println("but also not really implemented?");
}

void reverse(Reel * r, uint8_t n) {
  digitalWrite(K103_REVERSE, LOW);
  // TODO: only wait if we're actually flipping it
  delay(30);
  analogWrite(K103_TAKEUP, 127);
  digitalWrite(K103_ADVANCE, HIGH);
  delay(30);
  digitalWrite(K103_ADVANCE, LOW);
  delay(1200);
  digitalWrite(K103_TAKEUP, LOW);
  Serial.println("but also not really implemented?");
}

void handle_reel_command() {
  // TODO: timeout or other escape
  while (!Serial.available());
  byte c = Serial.read();
  switch (c) {
    case '?':
      while (!Serial.available());
      char device = Serial.read();
      if (device == 'C') {
        Serial.write((const char*)(&bolex), sizeof(Reel));
      } else {
        Serial.println("Not yet implemented");
      }
      return;
    default: return Serial.println("not yet implemented");
  }
}

void handle_frame_command() {
  // TODO: timeout or other escape
  while (!Serial.available());
  byte c = Serial.read();
  switch (c) {
    case '*': return Serial.println("not yet implemented");
    case 'F':
      while (!Serial.available());
      forward(&k103, Serial.read());
      return Serial.println("probably advanced");
    case 'R':
      while (!Serial.available());
      reverse(&k103, Serial.read());
      return Serial.println("probably advanced");
    case '?': return Serial.println("not yet implemented");
    default:
      Serial.print("bad frame command byte: ");
      Serial.println(c, HEX);
  }
}

void handle_serial() {
  if (Serial.available() > 0) {
    byte c = Serial.read();
    switch (c) {
      case ASCII_DC1: return handle_reel_command();
      case ASCII_DC2: return handle_frame_command();
      default:
        Serial.print("bad command byte: ");
        Serial.println(c, HEX);
    }
  }
}

void loop() {
  handle_serial();
}

