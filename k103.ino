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
 *    DC1 '?' 'C|P'
 * 
 * Frames (DC2)
 * - capture frames
 *    DC2 '!' 'C|P' N
 * 
 * - get frame number
 *    DC2 '?' 'C|P'
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

#include <EEPROM.h>
#include "packetizer.h"

#define BOLEX_SWITCH  12 // pulls down
#define BOLEX_SHUTTER 8  // active low
#define K103_ADVANCE  4  // active high "drive"
#define K103_REVERSE  7  // active low
#define K103_TAKEUP   9  // active high???
#define K103_FWD_SW   2  // pulls down
#define K103_REV_SW   3  // pulls down

#define K103_TAKEUP_DRIVE 127 // pwm level
#define K103_TAKEUP_ACCEL 6   // ms per pwm-level
#define K103_TAKEUP_SPINDOWN 10000  // ms
#define K103_RELAY_SETTLE 10  // ms
#define K103_FRAME_TIME 1200  // ms
#define BOLEX_FRAME_TIME 800  // ms
#define FRAME_UPDATE_THROTTLE 60000  // ms, save eeprom wear for higher data loss risk

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

char frame_advance_device = 0x00;
long frame_advance_current;
long frame_advance_target;
unsigned long frame_advance_last_update = 0;

int k103_drive_current = 0;
int k103_drive_target = 0;
unsigned long k103_drive_last_set = 0;

void dump_takeup(unsigned long now) {
  unsigned long dt = now - k103_drive_last_set;
  pk.log("Takup dump:");
  pk.log("dt:");
  pk.log(String(dt, DEC));
  pk.log("drive current:");
  pk.log(String(k103_drive_current, DEC));
  pk.log("drive target:");
  pk.log(String(k103_drive_target, DEC));
}

bool _persist_reel_frame(unsigned long now, Reel * r, boolean * dirty,
                         unsigned long * last_frame_save, uint16_t eep_offset,
                         boolean ignore_throttle) {
  boolean throttle_cleared = (now - *last_frame_save) > FRAME_UPDATE_THROTTLE;
  if (*dirty && (throttle_cleared || ignore_throttle)) {
    EEPROM.put(eep_offset + 28, r->frame);
    *dirty = false;
    *last_frame_save = now;
    pk.log("persisted reel frame");
  }
  return *dirty;
}
bool persist_frames(boolean ignore_throttle=false) {
  unsigned long now = millis();
  _persist_reel_frame(now, &bolex, &bolex_frame_dirty, &bolex_last_frame_save, EEP_BOLEX_OFFSET, ignore_throttle);
  _persist_reel_frame(now, &k103, &k103_frame_dirty, &k103_last_frame_save, EEP_K103_OFFSET, ignore_throttle);
}

void start_takeup(bool forward, unsigned long now) {
  k103_drive_last_set = now;
  int target = (forward ? 1 : -1) * K103_TAKEUP_DRIVE;
  if (k103_drive_target == target) {
    pk.log("takeup already at speed");
  } else {
    pk.log("takeup spin up");
    k103_drive_target = target;
  }
}

void update_takeup(unsigned long now) {
  unsigned long dt = now - k103_drive_last_set;
  bool takeup_is_forward = k103_drive_current > 0;
  bool target_forward = k103_drive_target > 0;
  bool relay_forward = digitalRead(K103_REVERSE) == HIGH;  // active-low
  bool right_direction = target_forward == relay_forward;
  bool up_to_speed = k103_drive_current == k103_drive_target;
  if (right_direction && up_to_speed) {
    // then spin-down
    if (dt >= K103_TAKEUP_SPINDOWN && k103_drive_target != 0) {
      pk.log("takeup spin down");
      k103_drive_target = 0;
      k103_drive_last_set = now;
    }
    return;
  }

  // deal with direction relay first
  if (k103_drive_current == 0 && !up_to_speed) {
    if (right_direction) {
      if (dt < K103_RELAY_SETTLE) {
        return;
      }
    } else {  // !right_direction
      pk.log("switch k103 drive direction");
      digitalWrite(K103_REVERSE, target_forward);  // active-low
      k103_drive_last_set = now;
      return;
    }
  }

  // then acceleration
  if (dt < K103_TAKEUP_ACCEL) {
    return;
  } else {
    if (!up_to_speed) {
      int adjustment;
      if (k103_drive_target == 0) {
        adjustment = takeup_is_forward ? -1 : 1;
      } else {
        adjustment = (k103_drive_target > 0) ? 1 : -1;
      }
      k103_drive_current += adjustment;
      analogWrite(K103_TAKEUP, abs(k103_drive_current));
      k103_drive_last_set = now;
      if (k103_drive_current == k103_drive_target) {
        if (k103_drive_current == 0) {
          digitalWrite(K103_REVERSE, HIGH);  // active-low
          pk.log("takeup at rest");
        } else {
          pk.log("takeup at speed");
        }
      }
      return;
    }
  }
}

bool takeup_ready(unsigned long now) {
  if (k103_drive_current == 0) {
    return false;
  }
  if (k103_drive_current != k103_drive_target) {
    return false;
  }
  k103_drive_last_set = now;
  return true;
}

void update_advances(unsigned long now) {
  unsigned long dt = now - frame_advance_last_update;
  Reel * r;
  unsigned long frame_timeout;
  int frame_pin;
  switch (frame_advance_device) {
  case 'P':
    if (!takeup_ready(now)) {
      return;
    }
    r = &k103;
    frame_timeout = K103_FRAME_TIME;
    break;
  case 'C':
    r = &bolex;
    frame_timeout = BOLEX_FRAME_TIME;
    break;
  case 0x00:
    return;
  default:
    pk.log("invalid frame advance device");
    pk.log(frame_advance_device);
    return;
  }
  if (dt < frame_timeout) {
    return;
  }
  if (frame_advance_current == frame_advance_target) {
    frame_advance_device = 0x00;
    persist_frames(true);
    pk.log("advance frames: done");
    return;
  }
  bool reverse = frame_advance_target < 0;
  int incr = reverse ? -1 : 1;
  frame_advance_current += incr;

  if (frame_advance_device == 'C') {
    digitalWrite(BOLEX_SHUTTER, LOW);
    delay(60);
    digitalWrite(BOLEX_SHUTTER, HIGH);
  } else {
    digitalWrite(K103_ADVANCE, HIGH);
    delay(30);
    digitalWrite(K103_ADVANCE, LOW);
  }
  update_frame(r, incr);

  pk.log("advance frame");
  pk.log(String(frame_advance_current, DEC));
  frame_advance_last_update = now;
}


void advance_k103(int n, unsigned long now) {
  if (frame_advance_device != 0x00) {
    pk.log("cannot advance k103: busy");
    return;
  }
  if (n == 0) {
    pk.log("k103 got invalid 'advance 0'");
    return;
  }
  start_takeup(n > 0, now);
  frame_advance_device = 'P';
  frame_advance_target = n;
  frame_advance_current = 0;
  frame_advance_last_update = now - K103_FRAME_TIME;
}

void advance_bolex(int n, unsigned long now) {
  if (frame_advance_device != 0x00) {
    pk.log("cannot advance bolex: busy");
    return;
  }
  if (n == 0) {
    pk.log("bolex got invalid 'advance 0'");
    return;
  }
  frame_advance_device = 'C';
  frame_advance_target = n;
  frame_advance_current = 0;
  frame_advance_last_update = now - BOLEX_FRAME_TIME;
}


void update_frame(Reel * r, int n) {
  r->frame += n;
  if (r == &bolex) {
    bolex_frame_dirty = true;
  } else if (r == &k103) {
    k103_frame_dirty = true;
  } else {
    pk.log("invlalid reel specified for frame update");
  }
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
//  dump_eep();

  restore_reel_state(&bolex, EEP_BOLEX_OFFSET);
  restore_reel_state(&k103, EEP_K103_OFFSET);
  
//  TCCR1B = TCCR1B & B11111000 | B00000010; // timer1 PWM frequency 3921.16 Hz
}

void fwd_isr() {
}
void rev_isr() {
}


void advance(char c, int n, unsigned long now) {
  if (c == 'C') {
    advance_bolex(n, now);
  } else if (c == 'P') {
    advance_k103(n, now);
  } else {
    pk.log("invalid c for advance");
    pk.log(c);
  }
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
  byte info[3 + sizeof(Reel)];
  info[0] = ASCII_DC1;
  info[1] = 'i';
  info[2] = c;
  memcpy(info + 3, r, sizeof(Reel));
  pk.send(info, 3 + sizeof(Reel));
}

void send_frame_no(char c, Reel * r) {
  byte info[3 + sizeof(long)];
  info[0] = ASCII_DC2;
  info[1] = 'i';
  info[2] = c;
  memcpy(info + 3, &(r->frame), sizeof(long));
  pk.send(info, 3 + sizeof(long));
}

void send_busy() {
  pk.log("busy");
}

void get_packet(unsigned long now) {
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
      if (frame_advance_device != 0x00) {
        return send_busy();
      }
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
    case '!':
      if (frame_advance_device != 0x00) {
        return send_busy();
      }
      advance(rec[2], *(int*)(rec + 3), now);
      return;
    case '?':
      if (rec[2] == 'C') {
        send_frame_no('C', &bolex);
      } else if (rec[2] == 'P') {
        send_frame_no('P', &k103);
      } else {
        pk.log("expected 'C' or 'P' for '?'");
        pk.log(rec[2], 1);
      }
      return;
    default:
      pk.log("bad frame command byte");
      pk.log(String(rec[0], DEC));
    }
    break;
  case '_':
    if (rec[1] == 'T') {
      dump_takeup(now);
    } else {
      pk.log("unrecognied thing for dump");
      pk.log(rec[1]);
    }
    return;
  default:
    pk.log("bad command byte");
    pk.log(String(rec[0], DEC));
  }
}

void loop() {
  unsigned long now = millis();
  if (pk.might_have_something()) {
    get_packet(now);
  }
  update_takeup(now);
  update_advances(now);
  persist_frames();
//  delay(10);
}

