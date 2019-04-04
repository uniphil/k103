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
 * Reels
 * - load film (ts, desc, len, curr frame)
 * 
 * Frames
 * - bolex: capture frame
 * - k103: advance reel
 * - k103: reverse reel
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

struct Reel {
  unsigned long ts;
  char desc[8];
  long len;
  long frame;
};

Reel bolex;
Reel k103;

void restore_reel_state(Reel * r, uint16_t eep_offset) {
  EEPROM.get(eep_offset + 0, r->ts);
  EEPROM.get(eep_offset + 4, r->desc);
  EEPROM.get(eep_offset + 12, r->len);
  EEPROM.get(eep_offset + 16, r->frame);
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
}

void fwd_isr() {
}
void rev_isr() {
}

void handle_serial() {
  if (Serial.available() > 0) {
    
  }
}

void loop() {
  handle_serial();
}

