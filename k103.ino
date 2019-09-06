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


Packetizer pk = Packetizer(&Serial);


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

void dump_eep(size_t start=0, size_t finish=512, size_t batch=32) {
  byte stuff[32];
  for (int i = start; i <= (finish - batch); i += batch) {
    for (int j = 0; j < batch; j++) {
      stuff[j] = EEPROM.read(i + j);
    }
    pk.log(stuff, batch);
  }
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

//  dump_eep();
//  pk.log("bolex");
//  dump_eep(EEP_BOLEX_OFFSET, EEP_BOLEX_OFFSET + 32);
//  pk.log("k103");
//  dump_eep(EEP_K103_OFFSET, EEP_K103_OFFSET + 32);
  

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

void handle_load_reel(Reel * r, byte * info, uint16_t eep_offset) {
  unsigned long ts = *(unsigned long *)info;
  char desc[20];
  strncpy(desc, info + 4, 20);
  long len = *(long*)(info + 24);
  long frame = *(long*)(info + 28);
  load_film(r, eep_offset, ts, desc, len, frame);
}


void send_reel_info(char c, Reel * r) {
  byte info[35];
  info[0] = ASCII_DC1;
  info[1] = 'i';
  info[2] = c;
  memcpy(info + 3, r, sizeof(Reel));
  pk.send(info, sizeof(Reel) + 3);
}

void get_packet() {
  byte rec[62];
  uint8_t len;
  pk.receive(rec, &len);
  switch (rec[0]) {
  case ASCII_DC1:  // reel command
    switch(rec[1]) {
    case '?':
      if (rec[2] == 'C') {
        send_reel_info('C', &bolex);
      } else if (rec[2] == 'P') {
        send_reel_info('P', &k103);
      } else {
        pk.log("expected 'C' or 'P' for '?'");
        pk.log(rec[2], 1);
      }
      return;
    case '!':
      if (rec[2] == 'C') {
        handle_load_reel(&bolex, rec + 3, EEP_BOLEX_OFFSET);
      } else if (rec[2] == 'P') {
        handle_load_reel(&k103, rec + 3, EEP_K103_OFFSET);
      } else {
        pk.log("expected 'C' or 'P' for '!'");
        pk.log(rec[2], 1);
      }
      return;
    default:
      pk.log("bad reel command byte");
      pk.log(String(rec[0], DEC));
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
      return pk.log("'?' not yet implemented");
    default:
      pk.log("bad frame command byte");
      pk.log(String(rec[0], DEC));
    }
    break;
  default:
    pk.log("bad command byte");
    pk.log(String(rec[0], DEC));
  }
}



void loop() {
  if (pk.might_have_something()) {
    get_packet();
  }
  persist_frames();
  delay(10);
}

