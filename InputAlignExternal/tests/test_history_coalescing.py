import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'worker'))
from predictor import Predictor,load_simulator
from protocol import Header,FRAME,state_tuple
from main import newer_frame

class HistoryTests(unittest.TestCase):
    def test_complete_queued_history_is_preserved(self):
        p=Predictor(load_simulator('kA2,0;'))
        state=p.sim.initial_state();frames=[]
        frames.append((Header(FRAME,1,2,1,0),state_tuple(p.sim.view(state)),0,0))
        for tick in range(1,166):
            state=p.sim.step(state,False)
            frames.append((Header(FRAME,1,2,1,tick),state_tuple(p.sim.view(state)),0,min(tick,64)))
        for frame in frames[:101]:p.synchronize(*frame)
        latest=frames[101]
        for frame in frames[102:]:latest=newer_frame(p,latest,frame)
        p.synchronize(*latest)
        self.assertEqual(p.tick,165)
        self.assertFalse(p.rejected)

    def test_actual_missing_history_still_rejects(self):
        p=Predictor(load_simulator('kA2,0;'))
        state=state_tuple(p.sim.view(p.state))
        with self.assertRaisesRegex(ValueError,'historial'):
            p.synchronize(Header(FRAME,1,2,1,65),state,0,64)

    def test_new_attempt_can_recover_after_rejected_session(self):
        p=Predictor(load_simulator('kA2,0;'))
        p.session=1;p.rejected=True
        state=p.sim.initial_state();latest=None
        for tick in range(1,66):
            state=p.sim.step(state,False)
            frame=(Header(FRAME,1,2,2,tick),state_tuple(p.sim.view(state)),0,min(tick,64))
            latest=frame if latest is None else newer_frame(p,latest,frame)
        p.synchronize(*latest)
        self.assertEqual(p.session,2)
        self.assertEqual(p.tick,65)
        self.assertFalse(p.rejected)
