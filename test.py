#!/usr/bin/env python
from serial import Serial
from serial.tools import list_ports
from time import sleep


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
        'R',
        1,
    ]))

    sleep(2)
    t = s.read(s.in_waiting)
    print(t)


    s.close()
