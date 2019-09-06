#include <Arduino.h>

#define PACKET_NORMAL   0b00 << 6
#define PACKET_RX_ERR   0b01 << 6
#define PACKET_TX_ERR   0b10 << 6
#define PACKET_USER_LOG 0b11 << 6


class Packetizer {
 public:
  Packetizer(Stream *s);

  boolean
    might_have_something();

  int
    receive(byte * out, uint8_t * out_length),
    send(byte * message, size_t message_length, uint8_t mode=PACKET_NORMAL),
    send(String message),
    log(byte * message, size_t message_length),
    log(String message);

  template < typename T > T &put(T &t) {
    send((uint8_t*)&t, sizeof(T));
    return t;
  }

 private:
  int
    unstuff(byte * bytes, uint8_t out_length),
    send_mode(String message, uint8_t mode=PACKET_NORMAL);

  Stream
    *stream;
};

