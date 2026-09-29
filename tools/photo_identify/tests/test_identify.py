import base64
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

path = Path(__file__).resolve().parents[1]/'identify_watch.py'
spec = importlib.util.spec_from_file_location('identify_watch', path)
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)

class IdentifyTests(unittest.TestCase):
    def frame(self, request=5):
        data = bytes([0, 248])*480*270
        b64 = base64.b64encode(data).decode()
        return (f'PICBEGIN id={request} w=480 h=270 bytes={len(data)} sum={sum(data)}\n' +
                ''.join('PIC:'+b64[i:i+72]+'\n' for i in range(0,len(b64),72)) +
                f'PICEND id={request}\n')

    def test_png_and_request_validation(self):
        self.assertTrue(m.parse_frame(self.frame(),5).startswith(b'\x89PNG'))
        with self.assertRaises(RuntimeError): m.parse_frame(self.frame(),6)
        with self.assertRaises(RuntimeError): m.parse_frame(self.frame().replace('PICEND','BAD'),5)

    def test_corruption_rejected(self):
        with self.assertRaises(RuntimeError):
            m.parse_frame(self.frame().replace('PIC:', 'BAD:',1),5)

    def test_result_chunks_bounded_and_utf8_preserved(self):
        text='这是一个水杯。'*100
        sent=[]
        with patch.object(m,'ack',side_effect=lambda c,t,r:sent.append(t)):
            m.send_result(None,4294967295,text)
        self.assertTrue(all(len(c.encode())+1<=63 for c in sent))
        data=b''.join(bytes.fromhex(c.split()[4]) for c in sent[1:-1])
        self.assertEqual(data,m.result_bytes(text));data.decode('utf-8')

    def test_status_timeout_configurable(self):
        from unittest.mock import Mock
        console = Mock(command_timeout=45)
        m.command(console, 'pictl q')
        self.assertEqual(console.run_command.call_args.args[1], 45)
        m.command(console, 'pictl f 1', 120)
        self.assertEqual(console.run_command.call_args.args[1], 120)

    def test_completion_budget_and_length_failure(self):
        for content in ['', '不完整结果']:
            with self.subTest(content=content), patch.dict(m.os.environ, {'MIMO_API_KEY':'test'}), \
                 patch.object(m, '_http_json', return_value={
                     'choices':[{'finish_reason':'length','message':{'content':content}}],
                     'usage':{'completion_tokens':8192}}) as http:
                with self.assertRaisesRegex(m.AiError, '--max-completion-tokens'):
                    m.analyze(b'png', 'direct', 8192)
                self.assertEqual(http.call_args.args[1]['max_completion_tokens'], 8192)

    def test_object_prompt_not_fall_prompt(self):
        with patch.dict(m.os.environ, {'MIMO_API_KEY':'test'}), patch.object(m,'_http_json',return_value={
            'choices':[{'message':{'content':'水杯，用于喝水。'}}]}) as http:
            self.assertEqual(m.analyze(b'png','direct'),'水杯，用于喝水。')
        self.assertEqual(http.call_args.args[1]['max_completion_tokens'],4096)
        self.assertIn('拍照识物助手',str(http.call_args))
        self.assertNotIn('fall_detected',str(http.call_args))

if __name__=='__main__':unittest.main()
