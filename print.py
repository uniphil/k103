#!/usr/bin/env python
from time import sleep
from serial import Serial
from serial.tools import list_ports
from k103 import REEL_CMD, FRAME_CMD

def capture(s, n):
    for frame in range(n):
        print(frame + 1)
        s.write(bytearray([FRAME_CMD, '*', 1]))
        sleep(0.9)
        s.write(bytearray([FRAME_CMD, 'F', 1]))
        sleep(1.3)



if __name__ == '__main__':
    import sys
    try:
        n = int(sys.argv[1])
    except IndexError:
        sys.stderr.write('Usage: {} N [PORT]\n\n'.format(sys.argv[0]))
        sys.stderr.write('N    - number of frames to capture\n')
        sys.stderr.write('PORT - serial port for arduino (should auto-detect)')
        sys.exit(1)
    except ValueError:
        sys.stderr.write('Usage: {} N [PORT]\n\n'.format(sys.argv[0]))
        sys.stderr.write('N    - number of frames to capture\n')
        sys.stderr.write('PORT - serial port for arduino (should auto-detect)\n\n')
        sys.stderr.write('Could not understand "{}" as a number for N'.format(sys.argv[1]))
        sys.exit(1)
    try:
        port = sys.argv[2]
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

    print('Capturing {} frames...'.format(n))
    capture(s, n)

    sleep(1)
    t = s.read(s.in_waiting)
    print(t)
    print('done!')

    s.close()
