"""Safety and integration tests using the real independent C++ simulator."""
from pathlib import Path
import json
import math
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'worker'))
from protocol import (HEADER, FRAME_BODY, DECISION_BODY, STATE, STATUS_BODY, FRAME, HELLO, STATUS,
                      Header, VALID, GROUND, decode_frame, decision, matches, state_tuple)
from predictor import Predictor, UnsupportedLevel, load_simulator


def frame_packet(header, state, history=0, count=0, horizon=0, budget=6000, options=0):
    return header.pack() + FRAME_BODY.pack(*state, history, count, horizon, budget, options)


class ProtocolTests(unittest.TestCase):
    def test_layout_and_rejection(self):
        self.assertEqual((HEADER.size, STATE.size, FRAME_BODY.size, DECISION_BODY.size), (48, 36, 60, 340))
        header = Header(FRAME, 0x11, 0x22, 7, 10)
        packet = frame_packet(header, (12.5, 105, -1.25, .9, VALID | GROUND), 5, 10, 400)
        self.assertEqual(decode_frame(packet, (0x11, 0x22))[0], header)
        for broken in [b'', packet[:-1], packet + b'0', b'BAD!' + packet[4:]]:
            with self.assertRaises(ValueError): decode_frame(broken, (0x11, 0x22))
        with self.assertRaises(ValueError): decode_frame(packet, (0, 0))
        for bad in [(float('nan'), 105, 0, .9, VALID), (0, 0, 0, .9, 15), (0, 0, 0, .9, 512)]:
            with self.assertRaises(ValueError): decode_frame(frame_packet(header, bad), (0x11, 0x22))

    def test_cpp_python_wire_interoperability(self):
        executable = os.environ.get('IAX_GATE_TEST')
        if not executable:
            self.skipTest('Set IAX_GATE_TEST to the compiled gate test executable')
        packet = subprocess.check_output([executable, '--frame'])
        header, actual, history, count, horizon, budget, _ = decode_frame(packet, (0x11, 0x22))
        self.assertEqual((header.session, header.tick, actual[0], history, count, horizon, budget),
                         (7, 10, 12.5, 5, 10, 400, 6000))
        response = decision(header, 10, actual, (25, 105, 0, .9, GROUND), 3, True, [])
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'decision.bin'; path.write_bytes(response)
            subprocess.run([executable, '--decision', str(path)], check=True)

    def test_nonfinite_state_never_matches(self):
        self.assertFalse(matches((math.nan, 0, 0, .9, 0), (0, 0, 0, .9, 0)))
        self.assertFalse(matches((0, 0, 0, .9, 16), (0, 0, 0, .9, 0)))


class PredictorTests(unittest.TestCase):
    def setUp(self):
        self.sim = load_simulator('kA2,0;')
        self.predictor = Predictor(self.sim)
        self.header = Header(FRAME, 1, 2, 1, 0)
        self.initial = state_tuple(self.sim.view(self.sim.initial_state()))

    def test_safe_click_passes_without_delay(self):
        state = (*self.initial[:4], self.initial[4] | VALID)
        self.assertIsNone(self.predictor.forecast(self.header, state, 0, 0, 80, 20000, 0))

    def test_native_branch_matches_reference_search(self):
        for raw in ['kA2,0;', 'kA2,0;1,8,2,30,3,55;']:
            for horizon in [80, 400]:
                with self.subTest(level=raw, horizon=horizon):
                    sim = load_simulator(raw)
                    predictor = Predictor(sim)
                    self.assertIsNotNone(predictor.native, 'Build gdsim using this project CMakeLists')
                    state = state_tuple(sim.view(sim.initial_state()))
                    state = (*state[:4], state[4] | VALID)
                    expected = predictor.forecast(self.header, state, 0, 0, horizon, 20000, 1, clock=lambda: 0.)
                    actual = predictor.forecast(self.header, state, 0, 0, horizon, 20000, 1)
                    self.assertEqual(actual, expected)

    def test_missing_history_and_reset(self):
        with self.assertRaisesRegex(ValueError, 'historial'):
            self.predictor.synchronize(Header(FRAME, 1, 2, 1, 65), self.initial, 0, 64)
        with self.assertRaises(ValueError):
            self.predictor.synchronize(self.header, self.initial, 0, 0)
        self.predictor.synchronize(Header(FRAME, 1, 2, 2), self.initial, 0, 0)
        self.assertFalse(self.predictor.rejected)

    def test_unsupported_notes_do_not_leak_into_next_attempt(self):
        from gdgen.sim.fast import FastSimulator
        from gdgen.sim.level import build_level
        sim = FastSimulator(build_level({'kA2': '0'}, [{'1': '286', '2': '100', '3': '15'}], triggers=False))
        predictor = Predictor(sim)
        initial = state_tuple(sim.view(sim.initial_state()))
        predictor.synchronize(self.header, initial, 0, 0)
        future = predictor.state
        for _ in range(100): future = sim.step(future, False)
        self.assertTrue(sim.unsupported)
        predictor.rejected = True
        predictor.synchronize(Header(FRAME, 1, 2, 2), initial, 0, 0)
        self.assertFalse(predictor.rejected)
        self.assertFalse(predictor.sim.unsupported)

    def test_divergence_rejects_until_next_attempt(self):
        wrong = (self.initial[0] + .2, *self.initial[1:])
        with self.assertRaisesRegex(ValueError, 'coincide'):
            self.predictor.synchronize(self.header, wrong, 0, 0)
        with self.assertRaises(ValueError): self.predictor.synchronize(self.header, self.initial, 0, 0)

    def test_replay_history_reconstructs_native_state(self):
        state = self.sim.initial_state()
        inputs = [False, True, True, False, False, True, False, False]
        history = 0
        for held in inputs:
            state = self.sim.step(state, held); history = history * 2 + held
        actual = state_tuple(self.sim.view(state))
        self.predictor.synchronize(Header(FRAME, 1, 2, 1, len(inputs)), actual, history, len(inputs))
        self.assertEqual(state_tuple(self.sim.view(self.predictor.state)), actual)

    def test_native_modes_match_python_reference_on_target_compiler(self):
        # Windows uses a different compiler/CRT from upstream's Linux tests; check its float behavior too.
        from gdgen.sim.collide import Simulator
        from gdgen.sim.fast import FastSimulator, player_fields
        from gdgen.sim.level import build_level
        for mode in range(8):
            for mini in (0, 1):
                for speed in range(5):
                    with self.subTest(mode=mode, mini=mini, speed=speed):
                        level = build_level({'kA2': str(mode), 'kA3': str(mini), 'kA4': str(speed)}, [], triggers=False)
                        reference, native = Simulator(level), FastSimulator(level)
                        p, c = reference.initial_state(), native.initial_state()
                        for tick in range(120):
                            hold = bool(tick // 11 % 2)
                            p, c = reference.step(p, hold), native.step(c, hold)
                            self.assertEqual(player_fields(p), native.view(c), f'tick {tick}')
                            if p.dead: break

    def test_timeout_returns_no_partial_decision(self):
        times = iter([0., 1.])
        actual = (*self.initial[:4], self.initial[4] | VALID)
        self.assertIsNone(self.predictor.forecast(self.header, actual, 0, 0, 400, 1000, 0,
                                                clock=lambda: next(times, 1.)))

    def test_disabled_mode_and_zero_horizon(self):
        self.assertIsNone(self.predictor.forecast(self.header, self.initial, 0, 0, 400, 6000, 0))
        actual = (*self.initial[:4], self.initial[4] | VALID)
        self.assertIsNone(self.predictor.forecast(self.header, actual, 0, 0, 0, 6000, 0))

    def test_unsupported_geometry_is_not_dropped(self):
        for raw in ['kA8,1;', 'kA10,1;', 'kA2,0;1,999999,2,100,3,15;', 'kA2,0;1,1346,2,100,3,15;']:
            with self.assertRaises(UnsupportedLevel): load_simulator(raw)

    def test_song_offset_is_not_a_two_player_flag(self):
        sim = load_simulator('kA2,0,kA13,1;')
        self.assertEqual(sim.view(sim.initial_state())['mode'], 'cube')

    def test_compressed_level_is_bounded_and_validated(self):
        import base64, gzip
        compressed = base64.urlsafe_b64encode(gzip.compress(b'kA2,0;')).decode()
        sim = load_simulator(compressed)
        self.assertEqual(sim.view(sim.initial_state())['mode'], 'cube')
        with self.assertRaises(ValueError): load_simulator('invalid-base64!')

    def test_geometry_is_bounded_before_index_allocation(self):
        from unittest.mock import patch
        import predictor
        for raw in ['kA2,0;1,1,2,100,3,15,32,1e300;', 'kA2,0;1,1,2,nan,3,15;',
                    'kA2,0;1,1,2,100,3,15,128,inf;']:
            with self.subTest(level=raw), patch.object(predictor, 'build_level') as build:
                with self.assertRaises(UnsupportedLevel): load_simulator(raw)
                build.assert_not_called()
        with patch.object(predictor, 'MAX_INDEX_ENTRIES', 10), patch.object(predictor, 'build_level') as build:
            with self.assertRaises(UnsupportedLevel):
                load_simulator('kA2,0;' + '1,1,2,100,3,15;' * 12)
            build.assert_not_called()

    def test_actual_correction_survives_full_horizon(self):
        # An early jump hits an overhead spike; waiting until it has passed avoids it.
        sim = load_simulator('kA2,0;1,8,2,30,3,55;')
        predictor = Predictor(sim)
        initial = state_tuple(sim.view(sim.initial_state()))
        actual = (*initial[:4], initial[4] | VALID)
        response = predictor.forecast(self.header, actual, 0, 0, 80, 20000, 1)
        self.assertIsNotNone(response)
        values = DECISION_BODY.unpack_from(response, HEADER.size)
        delay = values[10]
        self.assertGreater(delay, 0)
        self.assertLessEqual(delay, 80)
        state = sim.initial_state()
        for _ in range(4 + delay): state = sim.step(state, False)
        self.assertTrue(matches(state_tuple(sim.view(state)), values[5:10]))
        for _ in range(80):
            state = sim.step(state, True)
            self.assertFalse(sim.view(state)['dead'])


class IntegrationTests(unittest.TestCase):
    def test_separate_console_worker_udp_handshake_and_bad_packets(self):
        with tempfile.TemporaryDirectory() as tmp, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as game:
            game.bind(('127.0.0.1', 0)); game.settimeout(10)
            directory = Path(tmp)
            (directory / 'bridge.json').write_text(json.dumps({'version': 1, 'port': game.getsockname()[1],
                                                              'tokenHi': '11', 'tokenLo': '22'}))
            (directory / 'level.txt').write_text('kA2,0;')
            process = subprocess.Popen([sys.executable, str(ROOT / 'worker/main.py'), '--bridge', tmp],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                hello, endpoint = game.recvfrom(2048)
                self.assertEqual(HEADER.unpack(hello)[:4], (0x31584149, HELLO, 0x11, 0x22))
                game.sendto(b'bad packet', endpoint)
                sim = load_simulator('kA2,0;')
                initial = state_tuple(sim.view(sim.initial_state()))
                game.sendto(frame_packet(Header(FRAME, 0x11, 0x22, 1), initial), endpoint)
                def read_status(session):
                    deadline = time.monotonic() + 10
                    while time.monotonic() < deadline:
                        game.settimeout(max(.01, deadline - time.monotonic()))
                        data, _ = game.recvfrom(2048)
                        message = HEADER.unpack_from(data)
                        if message[1] == STATUS and message[4] == session:
                            return data
                    self.fail('Worker never sent STATUS before the overall deadline')
                response = read_status(1)
                code, text = STATUS_BODY.unpack_from(response, HEADER.size)
                self.assertEqual(code, 0, text)
                # A changed state must suspend prediction immediately, and a new attempt must recover.
                wrong = (initial[0] + 100, *initial[1:])
                game.sendto(frame_packet(Header(FRAME, 0x11, 0x22, 1), wrong), endpoint)
                response = read_status(1)
                self.assertEqual(STATUS_BODY.unpack_from(response, HEADER.size)[0], 1)
                game.sendto(frame_packet(Header(FRAME, 0x11, 0x22, 2), initial), endpoint)
                response = read_status(2)
                self.assertEqual(STATUS_BODY.unpack_from(response, HEADER.size)[0], 0)
                self.assertIsNone(process.poll())
            finally:
                process.terminate(); process.communicate(timeout=5)

    def test_original_settings_preserved(self):
        original = ROOT.parent / 'InputAlign-v2.7.1-alpha.1-400ticks-windows.geode'
        if not original.is_file(): self.skipTest('Original package not present in source-only distribution')
        with zipfile.ZipFile(original) as package: baseline = json.loads(package.read('mod.json'))
        manifest = json.loads((ROOT / 'mod.json').read_text(encoding='utf-8'))
        self.assertNotEqual(manifest['id'], baseline['id'])
        self.assertEqual(len(manifest['settings']), 38)
        for key, value in baseline['settings'].items():
            for prop in ['type', 'default', 'min', 'max', 'one-of']:
                self.assertEqual(value.get(prop), manifest['settings'][key].get(prop), (key, prop))


class PackagingTests(unittest.TestCase):
    def test_missing_pinned_resource_is_rejected_before_packaging(self):
        import importlib.util
        import json
        import tempfile
        from unittest.mock import patch
        script = Path(__file__).resolve().parents[1] / 'scripts' / 'package-windows.py'
        spec = importlib.util.spec_from_file_location('iax_package', script)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'worker/vendor').mkdir(parents=True)
            (root / 'worker/vendor/UPSTREAM.json').write_text(json.dumps({'files': ['missing-table.json']}))
            output = root / 'release.zip'
            with patch.object(module, 'ROOT', root):
                with self.assertRaisesRegex(ValueError, 'Missing pinned simulator file'):
                    module.package(root, root / 'runtime.zip', output)
            self.assertFalse(output.exists())

if __name__ == '__main__': unittest.main()
