#define BOLEX_SWITCH  12 // pulls down
#define BOLEX_SHUTTER 8  // active low
#define K103_ADVANCE  4  // active high "drive"
#define K103_REVERSE  7  // active low
#define K103_TAKEUP   9  // active high???
#define K103_FWD_SW   2  // pulls down
#define K103_REV_SW   3  // pulls down

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

//  attachInterrupt(digitalPinToInterrupt(K103_FWD_SW), fwd_int, FALLING);
//  attachInterrupt(digitalPinToInterrupt(K103_REV_SW), rev_int, FALLING);

  Serial.begin(9600);
  Serial.println("hello");
}

void fwd_int() {
  Serial.print(millis());
  Serial.println(" fwd int");
}

void rev_int() {
  Serial.print(millis());
  Serial.println(" rev int");
}

void loop() {

  analogWrite(K103_TAKEUP, 82);
  digitalWrite(K103_ADVANCE, HIGH);
  delay(30);
  digitalWrite(K103_ADVANCE, LOW);
  delay(1000);
  analogWrite(K103_TAKEUP, 0);

  delay(1500);
  digitalWrite(K103_REVERSE, LOW);
  delay(1500);

  analogWrite(K103_TAKEUP, 82);
  digitalWrite(K103_ADVANCE, HIGH);
  delay(30);
  digitalWrite(K103_ADVANCE, LOW);
  delay(1000);
  analogWrite(K103_TAKEUP, 0);
  delay(1000);

  digitalWrite(K103_REVERSE, HIGH);
  while(1);

//  digitalWrite(K103_TAKEUP, LOW);
//  delay(1000);
//
//  Serial.print(millis());
//  Serial.println(" triggering advance");
//  digitalWrite(K103_ADVANCE, HIGH);
//  delay(30);
//  digitalWrite(K103_ADVANCE, LOW);
//
////  unsigned long t = pulseIn(BOLEX_SWITCH, LOW);
////  Serial.print("pulsed atfter ");
////  Serial.println(t);
//
//  delay(1000);
//  Serial.print(millis());
//  Serial.println(" bye");
//  while (1);
}

