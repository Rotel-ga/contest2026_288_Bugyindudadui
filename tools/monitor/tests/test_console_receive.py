import io
import os
import sys
import threading
import time
import unittest
from unittest.mock import patch
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from board_console import BoardConsole


class ConsoleReceive(unittest.TestCase):
    def test_small_packets_wake_reader_and_are_preserved(self):
        host, device = os.pipe()
        os.set_blocking(host, False)
        console = BoardConsole('unused')
        console._fd = host
        console.trace = io.BytesIO()
        packets = [bytes([65 + i % 26]) * 64 for i in range(100)]
        expected = b''.join(packets) + b'DONE'

        def sender():
            for packet in packets:
                os.write(device, packet)
                time.sleep(0.001)
            os.write(device, b'DONE')

        worker = threading.Thread(target=sender)
        try:
            worker.start()
            actual = console._read_until(b'', lambda b: b.endswith(b'DONE'),
                                         time.time() + 5)
            self.assertEqual(actual, expected)
            self.assertEqual(console.trace.getvalue(), expected)
        finally:
            worker.join(timeout=5)
            os.close(host)
            os.close(device)

    def test_command_waits_for_prompt_after_echo(self):
        console = BoardConsole('unused')
        console._fd = 123
        reads = []
        writes = []

        def read_until(buf, done, end):
            reads.append(len(reads))
            if len(reads) == 1:
                result = b"\n@@01020304\r\n"
                self.assertTrue(done(result))
                return result
            if len(reads) == 2:
                self.assertFalse(done(buf))
                self.assertEqual(len(writes), 1)
                result = buf + b"nsh> "
                self.assertTrue(done(result))
                return result
            return buf + b"PASS one frame\n"

        def write(fd, data):
            writes.append(data)
            return len(data)

        with patch('board_console.os.urandom', return_value=b"\x01\x02\x03\x04"), \
             patch('board_console.os.write', side_effect=write), \
             patch('board_console.time.sleep'), \
             patch.object(console, '_read_until', side_effect=read_until), \
             patch.object(console, 'drain', return_value=b''):
            result = console.run_command('p4x_selftest --jpeg-capture', 5,
                                         ['PASS one frame'])
        self.assertIn('PASS one frame', result)
        self.assertEqual(b''.join(writes[1:]), b'p4x_selftest --jpeg-capture\n')

    def test_idle_deadline_returns_without_data(self):
        host, device = os.pipe()
        os.set_blocking(host, False)
        console = BoardConsole('unused')
        console._fd = host
        try:
            self.assertEqual(console._read_until(b'', lambda b: False,
                                                time.time() + 0.05), b'')
        finally:
            os.close(host)
            os.close(device)


if __name__ == '__main__':
    unittest.main()
