import base64
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import thumb_image
import jpeg_frame


class FrameParsers(unittest.TestCase):
    def setUp(self):
        self.data = b''.join(i.to_bytes(2, 'little') for i in range(32))
        self.payload = base64.b64encode(self.data).decode()
        self.header = 'camera_capture: thumb begin w=8 h=4 fmt=rgb565le bytes=64\n'
        self.footer = f'camera_capture: thumb end sum32=0x{sum(self.data):08x}\n'

    def frame(self, payload=None):
        return self.header + 'THUMB:' + (payload or self.payload) + '\n' + self.footer

    def test_valid(self):
        frame = thumb_image.parse(self.frame())
        self.assertEqual(frame.checksum, 'ok')
        self.assertEqual(frame.pixel_count, 32)

    def test_invalid_base64_is_recoverable(self):
        with self.assertRaises(thumb_image.ThumbError):
            thumb_image.parse(self.frame('A' * 5))

    def test_interleaved_trace_rejected(self):
        with self.assertRaises(thumb_image.ThumbError):
            thumb_image.parse(self.frame(self.payload[:20] + 'Elapsed time: 31\n' + self.payload[20:]))

    def test_missing_footer_rejected(self):
        with self.assertRaises(thumb_image.ThumbError):
            thumb_image.parse(self.frame().replace(self.footer, ''))

    def test_corrupt_checksum_rejected(self):
        with self.assertRaises(thumb_image.ThumbError):
            thumb_image.parse(self.frame().replace(self.footer, 'camera_capture: thumb end sum32=0x0\n'))

    def test_saved_failure_logs_raise_domain_errors(self):
        logs = Path(__file__).resolve().parents[3] / 'out/monitor/logs'
        for name in ['20260928-200544-00007.log', '20260928-200731-00001.log']:
            path = logs / name
            if not path.exists():
                continue
            parser = jpeg_frame if '200731' in name else thumb_image
            error = jpeg_frame.JpegError if parser is jpeg_frame else thumb_image.ThumbError
            with self.subTest(name=name), self.assertRaises(error):
                parser.parse(path.read_text())


if __name__ == '__main__':
    unittest.main()
