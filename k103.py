#!/usr/bin/env python
from struct import pack, unpack
from time import time, ctime, mktime

THIS_SOFTWARE_EPOCH = mktime((  # validity check: no reel could be loaded before
    2019, 04, 16, 0, 0, 0, 0, 0, -1))


class Reel(object):
    loaded_at = None
    description = None
    total_frames = None
    current_frame = None

    def __init__(self, loaded_at, description, total_frames, current_frame):
        assert loaded_at > THIS_SOFTWARE_EPOCH,\
            'invalid reel loading time: before this software was written'
        self.loaded_at = loaded_at

        assert len(description.encode('ascii')) <= 8,\
            'description cannot be longer than 8 ascii characters'
        self.description = description

        assert total_frames >= 0,\
            'total frames must be greater than zero'
        self.total_frames = total_frames

        assert isinstance(current_frame, int),\
            'current frame must be an integer'
        self.current_frame = current_frame

    def __str__(self):
        return '<Reel {} {} frame {} of {}>'.format(
            ctime(self.loaded_at),
            self.description,
            self.current_frame,
            self.total_frames)

    def to_bytes(self):
        ascii_desc = '{: <8}'.format(self.description).encode('ascii')[:8]
        return bytearray(
            pack('I', self.loaded_at) +
            ascii_desc +
            pack('i', self.total_frames) +
            pack('i', self.current_frame))

    @staticmethod
    def from_bytes(b):
        assert len(b) == 20, 'reel data must be 20 bytes long'
        loaded_at = unpack('I', b[0:4])[0]  # arduino ulong => uint (4 bytes)
        description = b[4:12].decode('ascii')  # 4 ascii bytes for description
        total_frames = unpack('i', b[12:16])[0]  # arduino long => int (4 bytes)
        current_frame = unpack('i', b[16:20])[0]  # arduino long => int (4 bytes)
        return Reel(loaded_at, description, total_frames, current_frame)



if __name__ == '__main__':
    r_ok = lambda: bytearray([
        145, # ts  apr 16 7pm
        98,  # ts
        182, # ts
        92,  # ts
        'h', # desc
        'e', # desc
        'l', # desc
        'o', # desc
        'r', # desc
        'e', # desc
        'e', # desc
        'l', # desc
        80,  # len
        0,   # len
        0,   # len
        0,   # len
        3,   # curr
        0,   # curr
        0,   # curr
        0,   # curr
    ])

    import unittest

    class TestStringMethods(unittest.TestCase):
        def assertStartsWith(self, s, head):
            return self.assertEqual(s[:len(head)], head)

        def test_init_validation(self):
            with self.assertRaises(AssertionError) as e:
                Reel(0, 'asdf', 1, 0)
            self.assertStartsWith(e.exception.message, 'invalid reel loading time')
            with self.assertRaises(AssertionError) as e:
                Reel(1555458014, 'too long string', 1, 0)
            self.assertStartsWith(e.exception.message, 'description cannot be longer')
            with self.assertRaises(AssertionError) as e:
                Reel(1555458014, 'asdf', -1, 0)
            self.assertStartsWith(e.exception.message, 'total frames must be greater')
            with self.assertRaises(AssertionError) as e:
                Reel(1555458014, 'asdf', 1, 'x')
            self.assertStartsWith(e.exception.message, 'current frame must be an')
            # should be ok:
            Reel(1555458014, 'asdf', 1, 0)

        def test_from_bytes(self):
            r = Reel.from_bytes(r_ok())
            self.assertEqual(r.loaded_at, 1555456657)
            self.assertEqual(r.description, 'heloreel')
            self.assertEqual(r.total_frames, 80)
            self.assertEqual(r.current_frame, 3)

        def test_to_bytes(self):
            r = Reel.from_bytes(r_ok())
            self.assertEqual(r.to_bytes(), r_ok())


    unittest.main()

    print r
    print unpack('i', r.to_bytes()[12:16])
