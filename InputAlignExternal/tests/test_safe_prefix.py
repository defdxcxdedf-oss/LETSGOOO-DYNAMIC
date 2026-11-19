import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'worker'))
from predictor import load_simulator, Predictor, UnsupportedLevel
from protocol import Header, FRAME, VALID, state_tuple

class PrefixTests(unittest.TestCase):
    def test_correction_before_distant_dual_and_solo(self):
        sim = load_simulator('kA2,0;1,8,2,20,3,45;1,286,2,5000,3,15;1,287,2,5500,3,15;')
        p = Predictor(sim)
        actual = state_tuple(sim.view(p.state))
        self.assertIsNotNone(p.forecast(Header(FRAME,1,2,1), (*actual[:4], actual[4]|VALID), 0,0,120,20000,0))

    def test_portal_crossing_never_certifies_future_or_poison_real_prefix(self):
        for oid in (286,287):
            sim = load_simulator(f'kA2,0;1,{oid},2,100,3,15;')
            p = Predictor(sim)
            actual = state_tuple(sim.view(p.state))
            self.assertIsNone(p.forecast(Header(FRAME,1,2,1), (*actual[:4],actual[4]|VALID),0,0,400,20000,0))
            self.assertFalse(sim.unsupported)
            self.assertFalse(p.rejected)
            state = sim.initial_state()
            for tick in range(1,100):
                state = sim.step(state,False)
                if sim.unsupported:
                    with self.assertRaises(ValueError):
                        p.synchronize(Header(FRAME,1,2,1,tick),state_tuple(sim.view(state)),0,64)
                    self.assertTrue(p.rejected)
                    break
            else:
                self.fail('portal must invalidate actual simulation')

    def test_unknown_custom_physics_is_not_guessed(self):
        with self.assertRaises(UnsupportedLevel):
            load_simulator('kA2,0;1,999999,2,100,3,15;')
        with self.assertRaises(UnsupportedLevel):
            load_simulator('kA8,1;')

    def test_cold_spawn_cycle_returns_deadline_without_poisoning_state(self):
        import ctypes
        from protocol import HEADER, DECISION_BODY
        raw = 'kA2,0;1,1,2,1000,3,15,57,5;1,1268,2,1,51,7;1,1268,2,0,51,7,57,7,62,1;1,901,2,0,51,5,28,1,57,7,62,1;'
        p = Predictor(load_simulator(raw))
        output = ctypes.create_string_buffer(b'X' * (HEADER.size + DECISION_BODY.size))
        before = output.raw
        self.assertEqual(p.native(p.sim._h,p.state._h,400,500,4,0,0,output),2)
        self.assertEqual(output.raw,before)
        self.assertFalse(p.sim.unsupported)
        self.assertEqual(p.sim.view(p.state)['tick'],0)

    def test_overshooting_move_is_not_pruned_by_nominal_reach(self):
        sim = load_simulator('kA2,0;1,1,2,30,3,15,57,5;1,901,2,0,51,5,28,5500,10,1,30,18;')
        engine = sim.level.engine
        self.assertEqual(len(engine._always), 1)

    def test_x_move_also_reachable_by_spawn_is_unbounded(self):
        sim = load_simulator('kA2,0;1,1,2,30,3,15,57,5;1,901,2,100,51,5,28,100,10,1,57,7;1,1268,2,10,51,7;')
        self.assertEqual(len(sim.level.engine._always), 1)
