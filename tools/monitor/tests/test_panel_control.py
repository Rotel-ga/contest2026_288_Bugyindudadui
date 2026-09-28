import sys
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from panel_control import PanelControl, State, parse


class Console:
    def __init__(self, replies):
        self.replies = iter(replies)
        self.commands = []

    def run_command(self, command, *args, **kwargs):
        self.commands.append(command)
        rev, requested = next(self.replies)
        return f'FGSTATE rev={rev} requested={int(requested)} ack={rev} pc=idle\nFGEND\n'


class PanelTests(unittest.TestCase):
    def test_start_waits_for_button(self):
        console = Console([(0, False), (0, False), (1, True), (1, True)])
        with patch('panel_control.time.sleep'):
            self.assertEqual(PanelControl(console).wait_start(0), State(1, True))
        self.assertEqual(console.commands, ['fgctl query', 'fgctl ack 0 idle',
                                            'fgctl query', 'fgctl ack 1 running'])

    def test_stop_during_ack_prevents_capture(self):
        console = Console([(1, True), (2, False), (2, False), (2, False),
                           (3, True), (3, True)])
        with patch('panel_control.time.sleep'):
            self.assertEqual(PanelControl(console).wait_start(0), State(3, True))

    def test_stop_during_frame_acknowledged(self):
        console = Console([(2, False), (2, False)])
        panel = PanelControl(console)
        panel.finish(State(1, True), True)
        self.assertEqual(console.commands, ['fgctl ack 1 ok', 'fgctl ack 2 idle'])

    def test_once_waits_for_new_start_revision(self):
        console = Console([(1, True), (1, True), (3, True), (3, True)])
        panel = PanelControl(console, once=True)
        panel.finish(State(1, True), True)
        with patch('panel_control.time.sleep'):
            self.assertEqual(panel.wait_start(0), State(3, True))

    def test_direct_result_reaches_panel(self):
        import ai_client
        result = {"choices": [{"message": {"content":
                  '{"fall_detected":true,"confidence":0.91,"reason":"test"}'}}]}
        with patch('ai_client._http_json', return_value=result) as http:
            raw = ai_client.MimoClient(backend='direct', api_key='test-only').analyze(
                'test-image', 1, 'jpeg')
        self.assertIn('data:image/jpeg;base64,test-image', str(http.call_args))
        fall, confidence, reason = ai_client.parse_verdict(raw)
        console = Console([(1, True)])
        PanelControl(console).finish(State(1, True), True, fall=fall)
        self.assertEqual(console.commands, ['fgctl ack 1 fall'])
        self.assertEqual(confidence, 0.91)

    def test_model_error_does_not_report_normal(self):
        console = Console([(1, True)])
        PanelControl(console).finish(State(1, True), False)
        self.assertEqual(console.commands, ['fgctl ack 1 error'])

    def test_incomplete_response_rejected(self):
        with self.assertRaises(RuntimeError):
            parse('FGSTATE rev=1 requested=1 ack=0 pc=idle\n')


if __name__ == '__main__':
    unittest.main()
