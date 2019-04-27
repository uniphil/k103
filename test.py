#!/usr/bin/env python
from serial import Serial
from serial.tools import list_ports
from time import sleep, time

from k103 import Reel

CAM_CMD = 0x11  # DC1
PROJ_CMD = 0x12  # DC2



def check_load_bolex(s):
    r = Reel(time(), 'asdf', 72, 0)
    s.write(bytearray([
        CAM_CMD,
        '!',
        'C',
    ]))
    s.write(r.to_bytes())
    sleep(0.3)
    t = s.read(s.in_waiting)
    print(t)

def check_bolex_reel(s):
    s.write(bytearray([
        CAM_CMD,
        '?',
        'C',  # camera
    ]))
    sleep(1)
    # out = s.read(s.in_waiting)
    out = s.read(20)
    reel = Reel.from_bytes(out)
    print 'reel', reel
    # print('got', out)

def check_k103_reel(s):
    cmd = bytearray([
        CAM_CMD,
        '?',
        'P',  # projector
    ])


def check_load_k103(s):
    pass


def check_capture(s):
    pass


def check_advance(s):
    cmd = bytearray([
        PROJ_CMD,
        'F',
        2,
    ])
    s.write(cmd)
    sleep(3)
    t = s.read(s.in_waiting)
    print(t)



def check_reverse(s):
    cmd = bytearray([
        PROJ_CMD,
        'R',
        2,
    ])
    s.write(cmd)
    sleep(3)
    t = s.read(s.in_waiting)
    print(t)


def check_get_frame(s):
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

    # print('check load bolex')
    # check_load_bolex(s)
    # sleep(0.3)
    # print('check bolex reel')
    # check_bolex_reel(s)

    print('check advance')
    check_advance(s)

    print('check reverse')
    check_reverse(s)

    # s.write(bytearray([
    #     0x12,
    #     'F',
    #     1,
    # ]))

    # sleep(2)
    # t = s.read(s.in_waiting)
    # print(t)


    s.close()
