#include <Arduino.h>

#define FLAG_BYTE 0x7E
#define CONTROL_ESCAPE 0x7D
#define FLAGGED_REPLACE 0x5E
#define CONTROLLED_REPLACE 0x5D
#define CONTROL_RESTORE 0x20

#define UNSTUFFED_BUFFER_SIZE 64
#define STUFFED_BUFFER_SIZE (UNSTUFFED_BUFFER_SIZE * 2)

#define ERROR_UNSTUFF_ILLEGAL_FLAG_BYTE -1

#define TX_LOG_FRAMER 0x80
#define TX_LOG_USER   0x81

class Framer {
 public:
  Framer(Stream *s);

  size_t
    write(byte command, uint8_t * c, size_t s),
    print(String s),
    print(byte command, String s);

  boolean
    poll();

  Stream
    *stream;

 private:
  int
    stuff(byte * input, byte * output, size_t input_length, size_t * output_index),
    unstuff(byte * input, byte * output, size_t input_length, size_t * output_index);

  void
    handle_command(byte command, byte * data, uint8_t data_length);

  boolean
    frame_started,
    command_read,
    data_length_read,
    handle_receive_data(byte * buff, size_t len);

  uint8_t
    command,
    data_length,
    data_buffer[STUFFED_BUFFER_SIZE],
    data_buffer_index;
};

