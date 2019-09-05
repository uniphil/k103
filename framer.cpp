#include "framer.h"


Framer::Framer(Stream *s) :
  stream(s),
  frame_started(false),
  command_read(false),
  data_length_read(false),
  data_buffer_index(0) {}


boolean Framer::poll() {
  byte raw_buffer[STUFFED_BUFFER_SIZE];
  size_t n = stream->readBytes(raw_buffer, stream->available());
  if (n > 0) {
    return handle_receive_data(raw_buffer, n);
  }
  return false;
}


boolean Framer::handle_receive_data(byte * buff, size_t len) {
  size_t i = 0, output_length;
  byte output[UNSTUFFED_BUFFER_SIZE];

  if (!frame_started) {
    for (; i < len && buff[i] != FLAG_BYTE; i++);
    if (i > 0) {
      write(TX_LOG_FRAMER, "got garbage:", 12);
      write(TX_LOG_FRAMER, buff, i);
    }
    if (i == len) {
      return false;
    }
    frame_started = true;
    i += 1;  // flag byte
  }

  if (!command_read) {
    if (i == len) {
      return false;
    }
    command = buff[i];
    command_read = true;
    i += 1;
  }

  if (!data_length_read) {
    if (i == len) {
      return false;
    }
    data_length = buff[i];
    data_length_read = true;
    i += 1;
  }

  for (; i < len && data_buffer_index < data_length;) {
    data_buffer[data_buffer_index] = buff[i];
    data_buffer_index += 1;
    i += 1;
    // TODO: watch out for flag byte
  }
  if (data_length - data_buffer_index > 0) {
    return false;
  }

  frame_started = false;
  command_read = false;
  data_length_read = false;
  data_buffer_index = 0;

  if (i < len) {
    write(TX_LOG_FRAMER, "ohno", 4);
  }

  unstuff(data_buffer, output, data_length, &output_length);

  handle_command(command, output, output_length);
  return true;
}

//void Framer::handle_command(byte command, byte * data, uint8_t data_length) {
//  if (command == RX_ECHO) {
//    write(TX_LOG_FRAMER, "echo:", 5);
//    write(TX_LOG_FRAMER, data, data_length);
//  }
//}

size_t Framer::write(byte command, uint8_t * c, size_t s) {
  byte out_buffer[STUFFED_BUFFER_SIZE];
  size_t out_length;

  stream->write(FLAG_BYTE);
  stream->write(command);
  stuff(c, out_buffer, s, &out_length);
  stream->write(out_length);
  return stream->write(out_buffer, out_length);
}


size_t Framer::print(String s) {
  return print(TX_LOG_USER, s);
}


size_t Framer::print(byte command, String s) {
  return write(command, s.c_str(), s.length());
}


Framer::stuff(byte * input, byte * output, size_t input_length, size_t * output_index) {
  size_t input_index = 0;
  *output_index = 0;
  while (input_index < input_length) {
    switch (input[input_index]) {
    case FLAG_BYTE:
      output[*output_index] = CONTROL_ESCAPE;
      output[++*output_index] = FLAGGED_REPLACE;
      break;
    case CONTROL_ESCAPE:
      output[*output_index] = CONTROL_ESCAPE;
      output[++*output_index] = CONTROLLED_REPLACE;
      break;
    default:
      output[*output_index] = input[input_index];
    }
    input_index += 1;
    *output_index += 1;
  }
  return 0;
}

Framer::unstuff(byte * input, byte * output, size_t input_length, size_t * output_index) {
  size_t input_index = 0;
  *output_index = 0;
  while (input_index < input_length) {
    switch (input[input_index]) {
    case FLAG_BYTE:
      return ERROR_UNSTUFF_ILLEGAL_FLAG_BYTE;
    case CONTROL_ESCAPE:
      output[*output_index] = input[++input_index] ^ CONTROL_RESTORE;
      break;
    default:
      output[*output_index] = input[input_index];
    }
    input_index += 1;
    *output_index += 1;
  }
  return 0;
}

