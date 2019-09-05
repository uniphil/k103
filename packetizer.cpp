#include "packetizer.h"

Packetizer::Packetizer(Stream *s) : stream(s) {}

int print_packet(Stream &s, String m, uint8_t mode=PACKET_NORMAL);
int send_packet(Stream &s, byte * message, size_t message_length, uint8_t mode=PACKET_NORMAL);

boolean Packetizer::might_have_something() {
  return stream->available() > 0;
}

int Packetizer::send(byte * message, size_t message_length, uint8_t mode=PACKET_NORMAL) {
  if (message_length == 0 || message_length > 62) {
    send("invalid packet length", PACKET_TX_ERR);
    send(String(message_length, DEC), PACKET_TX_ERR);
    return -1;
  }

  stream->write((byte)0x00);
  stream->write(message_length + 1 | mode);

  uint8_t sequence_start = 0,
          sequence_length;
  while (sequence_start <= message_length) {
    sequence_length = 0;
    while (message[sequence_start + sequence_length] != 0x00 &&
           sequence_start + sequence_length < message_length) {
      sequence_length++;
    }
    stream->write(sequence_length + 1);
    stream->write(message + sequence_start, sequence_length);
    sequence_start += sequence_length + 1;
  }
}

int Packetizer::send(String message, uint8_t mode=PACKET_NORMAL) {
  return send(message.c_str(), message.length(), mode);
}

int Packetizer::debug(byte * message, size_t message_length) {
  return send(message, message_length, PACKET_USER_LOG);
}

int Packetizer::debug(String message) {
  return send(message.c_str(), message.length(), PACKET_USER_LOG);
}

int Packetizer::receive(byte * out, uint8_t * out_length) {
  bool packet_started = false;
  byte next_byte;
  *out_length = 0;
  uint8_t out_index = 0;
  uint8_t packet_mode;
  while (*out_length == 0 || out_index < *out_length) {
    stream->readBytes(&next_byte, 1);
    if (next_byte == 0x00) {
      if (packet_started) {
        send("Incomplete packet", PACKET_RX_ERR);
        send(out, *out_length, PACKET_RX_ERR);
      }
      *out_length = 0;
      out_index = 0;
      packet_started = true;
      continue;
    }
    if (!packet_started) {
      send("Unexpected byte", PACKET_RX_ERR);
      send(&next_byte, 1, PACKET_RX_ERR);
      continue;
    }
    if (*out_length == 0) {
      packet_mode = next_byte & (0xFF << 6);
      *out_length = next_byte & (0xFF >> 2);
      continue;
    }
    out[out_index] = next_byte;
    out_index += 1;
  }
  *out_length -= 1;
  unstuff(out, *out_length);
}


int Packetizer::unstuff(byte * bytes, uint8_t out_length) {
  uint8_t sequence_length,
          i = 0;
  while (i < out_length) {
    sequence_length = bytes[i];
    while (sequence_length--) {
      bytes[i] = bytes[i+1];
      i += 1;
    }
    bytes[i] = 0x00;
    i += 1;
  }
}

