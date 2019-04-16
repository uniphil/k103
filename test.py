#!/usr/bin/env python
from serial import Serial
from serial.tools import list_ports
from time import sleep

REEL_CMD = 0x11  # DC1
FRAME_CMD = 0x12  # DC2


def check_bolex_reel():
    cam_cmd = bytearray([
        REEL_CMD,
        '?',
        'C',
    ])
    return expect_cmd(cam_cmd, {

    })

def check_k103_reel():
    k103_cmd = bytearray([
        REEL_CMD,
        '?',
        'C',
    ])


def check_load_bolex():
    pass


def check_load_k103():
    pass


def check_capture():
    pass


def check_advance():
    pass


def check_reverse():
    pass


def check_get_frame():
    pass


if __name__ == '__main__':
    import sys
    try:
        port = sys.argv[1]
    except IndexError:
        maybes = list(list_ports.grep('usb'))
        if len(maybes) == 0:
            sys.stderr.write('missing serial port (probably /dev/tty.usbserial-something)\n')
            sys.exit(1)
        if len(maybes) > 1:
            sys.stderr.write('not sure which serial port to use. likely candidates:\n{}\n'.format(
                '\n'.join(map(lambda m: '{}\t{}\t{}'.format(m.device, m.description, m.manufacturer), maybes))))
            sys.exit(1)
        port = maybes[0].device

    s = Serial(port, 9600)

    sleep(2)

    t = s.read(s.in_waiting)
    print(t)

    s.write(bytearray([
        0x12,
        'F',
        1,
    ]))

    sleep(2)
    t = s.read(s.in_waiting)
    print(t)


    s.close()
