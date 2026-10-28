"""投递器命令行：动作表与客户端能力一致，各动作拼出的命令字段稳定。"""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import unittest

SCRIPTS = Path(__file__).resolve().parents[1]


def load_sender():
    spec = importlib.util.spec_from_file_location('sender_under_test', SCRIPTS / 'paraview_bridge_send.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class SenderCliTests(unittest.TestCase):
    def setUp(self):
        self.sender = load_sender()
        self.sent = []

        def fake_send(directory, action, color=None, timeout=60, options=None):
            self.sent.append((directory, action, color, timeout, options))
            return {'ok': True, 'id': 'x', 'value': {}}
        self.sender.send = fake_send

    def run_cli(self, *argv):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = self.sender.main(list(argv))
        return code, self.sent[-1]

    def test_action_registry_matches_client_capabilities(self):
        capabilities = set(self.sender.client_manifest()['capabilities'])
        self.assertEqual(set(self.sender.BRIDGE_ACTIONS) | {'reload_client'}, capabilities)

    def test_runtime_dispatch_covers_every_capability(self):
        """运行层分发表加上自处理动作，必须恰好等于对外声明的能力。"""
        import ast
        tree = ast.parse((SCRIPTS / 'paraview_bridge_runtime.py').read_text(encoding='utf-8'))
        values = {}
        for node in tree.body:
            if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name):
                if node.targets[0].id == 'CAPABILITIES':
                    values['capabilities'] = set(ast.literal_eval(node.value))
                elif node.targets[0].id == 'OPERATIONS':
                    values['operations'] = {key.value for key in node.value.keys}
        self_handled = {'inspect', 'background', 'restore_background', 'reload_client'}
        self.assertEqual(values['operations'] | self_handled, values['capabilities'])
        self.assertFalse(values['operations'] & self_handled)

    def test_actions_without_options_send_none(self):
        for action in ('inspect', 'restore_background'):
            code, sent = self.run_cli('current', action)
            self.assertEqual((code, sent[4]), (0, None))
        code, sent = self.run_cli('current', 'background', '--color', '0.1', '0.2', '0.3')
        self.assertEqual(sent[2], [0.1, 0.2, 0.3])

    def test_export_centerline_options(self):
        _, sent = self.run_cli('current', 'export_centerline', '--input', '/a', '--output', '/b.csv')
        self.assertEqual(sent[4], {'input': '/a', 'output': '/b.csv', 'resolution': 1024, 'max_level': 2})

    def test_add_vorticity_view_keeps_zero_slice_position(self):
        _, sent = self.run_cli('current', 'add_vorticity_view', '--source', 'S', '--slice-position', '0')
        self.assertEqual(sent[4], {'source': 'S', 'slice_axis': 'y', 'component': 'Magnitude',
                                   'slice_position': 0.0})
        _, sent = self.run_cli('current', 'add_vorticity_view', '--source', 'S',
                               '--color-range', '0', '1')
        self.assertEqual(sent[4]['color_range'], [0.0, 1.0])
        self.assertNotIn('slice_position', sent[4])

    def test_title_and_repair_options(self):
        _, sent = self.run_cli('current', 'repair_vorticity_slice', '--source', 'S')
        self.assertEqual(sent[4], {'source': 'S'})
        _, sent = self.run_cli('current', 'add_axis_title_view', '--source', 'S',
                               '--reset-view', 'v1', '--output', '/r.png')
        self.assertEqual(sent[4], {'source': 'S', 'reset_view': 'v1', 'output': '/r.png'})
        _, sent = self.run_cli('current', 'add_axis_title_view', '--source', 'S')
        self.assertEqual(sent[4], {'source': 'S'})

    def test_publication_options(self):
        _, sent = self.run_cli('current', 'publication_vorticity', '--source', 'S',
                               '--color-range', '0', '10', '--image-size', '2066', '1066',
                               '--view-id', 'v', '--output', '/o.png', '--style', 'reference')
        self.assertEqual(sent[4], {'source': 'S', 'color_range': [0.0, 10.0], 'vorticity_scale': 1.0,
                                   'style': 'reference', 'image_size': [2066, 1066],
                                   'view_id': 'v', 'output': '/o.png'})

    def test_missing_required_options_exit_with_usage_error(self):
        for argv in (('export_centerline', '--input', '/a'),
                     ('add_vorticity_view',),
                     ('publication_vorticity', '--source', 'S'),
                     ('repair_vorticity_slice',)):
            with self.subTest(argv=argv), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as raised:
                    self.sender.main(['current', *argv])
                self.assertEqual(raised.exception.code, 2)
        self.assertEqual(self.sent, [])

    def test_removed_gradient_export_is_rejected(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            self.sender.main(['current', 'export_gradient_script', '--output', '/x'])

    def test_failed_reply_returns_nonzero(self):
        self.sender.send = lambda *args, **kwargs: {'ok': False, 'id': 'x', 'error': 'boom'}
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(self.sender.main(['current', 'inspect']), 1)

    def test_option_whitelist_covers_every_action_field(self):
        for spec in self.sender.BRIDGE_ACTIONS.values():
            for group in spec.values():
                self.assertLessEqual(set(group), self.sender.BRIDGE_OPTION_KEYS)
        self.assertIn('expected_sha256', self.sender.BRIDGE_OPTION_KEYS)


if __name__ == '__main__':
    unittest.main()
