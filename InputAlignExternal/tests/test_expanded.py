"""Regression coverage for mechanics enabled in 0.2; real GD fidelity requires a game test."""
import ctypes
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'worker'))
from predictor import load_simulator, Predictor, UnsupportedLevel
from protocol import Header, FRAME, VALID, HELD, HEADER, DECISION_BODY, state_tuple, matches
from gdgen.sim.fast import FastSimulator, player_fields
from gdgen.sim.collide import Simulator
from gdgen.sim.level import build_level, dynamic_groups
from gdgen.sim.triggers import TriggerEngine, parse_triggers

class ExpandedMechanicsTests(unittest.TestCase):
    def test_correction_in_every_vehicle_survives_full_horizon(self):
        fixtures = [(20,45),(30,45),(20,45),(20,45),(20,25),(20,45),(20,35),(30,45)]
        for mode, (x,y) in enumerate(fixtures):
            with self.subTest(mode=mode):
                sim = load_simulator(f'kA2,{mode};1,8,2,{x},3,{y};')
                predictor = Predictor(sim)
                initial = state_tuple(sim.view(sim.initial_state()))
                answer = predictor.forecast(Header(FRAME,1,2,1), (*initial[:4],initial[4]|VALID), 0,0,120,20000,1)
                self.assertIsNotNone(answer)
                fields = DECISION_BODY.unpack_from(answer, HEADER.size)
                delay,down = fields[10:12]
                state = sim.initial_state()
                for _ in range(4+delay): state=sim.step(state,False)
                self.assertTrue(matches(state_tuple(sim.view(state)), fields[5:10]))
                for _ in range(120):
                    state=sim.step(state,bool(down))
                    self.assertFalse(sim.view(state)['dead'])

    def test_between_step_button_edge_is_not_physics_divergence(self):
        sim=load_simulator('kA2,0;'); predictor=Predictor(sim)
        actual=state_tuple(sim.view(sim.initial_state()))
        actual=(*actual[:4],actual[4]|HELD|VALID)
        predictor.forecast(Header(FRAME,1,2,1),actual,0,0,80,20000,0)
        self.assertFalse(predictor.rejected)
        next_state=sim.step(sim.initial_state(),True)
        predictor.synchronize(Header(FRAME,1,2,1,1),state_tuple(sim.view(next_state)),1,1)
        self.assertFalse(predictor.rejected)

    def test_hypothetical_unsupported_branch_does_not_disable_real_attempt(self):
        sim=FastSimulator(build_level({},[{'1':'286','2':'100','3':'15'}],triggers=False))
        predictor=Predictor(sim); initial=state_tuple(sim.view(sim.initial_state()))
        self.assertIsNone(predictor.forecast(Header(FRAME,1,2,1),(*initial[:4], initial[4]|VALID),0,0,400,20000,0))
        self.assertFalse(sim.unsupported)
        self.assertFalse(predictor.rejected)
        predictor.synchronize(Header(FRAME,1,2,1),initial,0,0)

    def test_current_held_input_is_passed_to_native_release_search(self):
        for raw in ['kA2,1;1,8,2,50,3,0;', 'kA2,4;1,8,2,10,3,0;', 'kA2,5;1,8,2,30,3,5;']:
            sim=load_simulator(raw); predictor=Predictor(sim)
            state=state_tuple(sim.view(sim.initial_state()))
            actual=(*state[:4], state[4]|HELD|VALID)
            native=predictor.forecast(Header(FRAME,1,2,1),actual,0,0,120,20000,1)
            reference=predictor.forecast(Header(FRAME,1,2,1),actual,0,0,120,20000,1,clock=lambda:0.)
            self.assertIsNotNone(native)
            self.assertEqual(native,reference)
            self.assertEqual(DECISION_BODY.unpack_from(native,HEADER.size)[11],0)

    def test_old_or_incomplete_native_adapter_is_reported_without_calling_it(self):
        from types import SimpleNamespace
        for library in [SimpleNamespace(), SimpleNamespace(iax_adapter_version=lambda:1),
                        SimpleNamespace(iax_adapter_version=lambda:2)]:
            with self.assertRaises(RuntimeError):
                Predictor(SimpleNamespace(initial_state=lambda:None, _lib=library))

    def test_reference_preview_notes_are_also_isolated(self):
        sim=FastSimulator(build_level({},[{'1':'286','2':'100','3':'15'}],triggers=False))
        predictor=Predictor(sim); initial=state_tuple(sim.view(sim.initial_state()))
        predictor.forecast(Header(FRAME,1,2,1),(*initial[:4], initial[4]|VALID),0,0,400,20000,0,clock=lambda:0.)
        self.assertFalse(sim.unsupported)
        self.assertFalse(predictor.rejected)

    def test_precompiled_adapter_is_required_before_level_allocation(self):
        import os
        from unittest.mock import patch
        import predictor
        with patch.dict(os.environ), patch.object(predictor, 'FastSimulator') as constructor:
            os.environ.pop('GDGEN_SIM_LIB',None)
            with self.assertRaisesRegex(RuntimeError,'CMake'): load_simulator('kA2,0;')
            constructor.assert_not_called()

    def test_full_inspection_survives_more_than_128_dynamic_activations(self):
        # Many one-use gravity portals are consumed at once and retained because their group can move.
        raw='kA2,0;'+''.join('1,11,2,0,3,15,57,5;' for _ in range(140))+'1,901,2,10000,51,5,29,30,10,1;'
        sim=load_simulator(raw);state=sim.step(sim.initial_state(),False)
        full=sim.view(state)
        self.assertGreater(len(full['activated']),128)
        self.assertNotIn('activated',sim.view(state,include_lists=False))
        predictor=Predictor(sim)
        predictor.synchronize(Header(FRAME,1,2,1,1),state_tuple(full),0,1)
        self.assertFalse(predictor.rejected)

    def test_states_cannot_cross_simulator_owners(self):
        a,b=load_simulator('kA2,0;'),load_simulator('kA2,0;')
        state=a.initial_state()
        with self.assertRaises(ValueError): b.step(state,False)
        with self.assertRaises(ValueError): b.view(state)

    def test_move_toggle_spawn_stop_and_vertical_teleport_match_reference(self):
        cases=[
            'kA2,0;1,1,2,120,3,15,57,5;1,901,2,20,51,5,29,300,10,0.1;',
            'kA2,0;1,1,2,120,3,15,57,5;1,1049,2,20,51,5,56,0;',
            'kA2,0;1,1,2,120,3,15,57,5;1,1268,2,20,51,7,63,0.1;1,1049,2,20,51,5,56,0,57,7,62,1;',
            'kA2,0;1,1,2,120,3,15,57,5;1,901,2,20,51,5,29,300,10,1;1,1616,2,60,51,5;',
            'kA2,0;1,747,2,100,3,45,54,300;',
            'kA2,0;1,1,2,1000,3,15,57,5;1,1268,2,20,51,7;1,1268,2,40,51,7;1,901,2,20,51,5,28,-300,10,0,57,7,62,1;',
        ]
        for raw in cases:
            with self.subTest(level=raw):
                native=load_simulator(raw); ref=Simulator(native.level)
                p,c=ref.initial_state(),native.initial_state()
                for tick in range(250):
                    p=ref.step(p,False);c=native.step(c,False)
                    self.assertEqual(player_fields(p),native.view(c),f'tick {tick}')
                    if p.dead:break

    def test_all_supported_move_easing_curves_match_target_compiler(self):
        steps=0
        for easing in range(19):
            raw=f'kA2,0;1,1,2,0,3,-15,128,10,57,5;1,901,2,0,51,5,29,30,10,0.5,30,{easing},85,2.3;'
            native=load_simulator(raw);reference=Simulator(native.level)
            p,c=reference.initial_state(),native.initial_state()
            for tick in range(150):
                p=reference.step(p,False);c=native.step(c,False)
                self.assertEqual(player_fields(p),native.view(c),f'easing={easing},tick={tick}')
                steps+=1
                if p.dead:break
        self.assertGreater(steps,500)

    def test_repeated_spawn_moves_are_not_pruned_by_single_move_reach(self):
        raw='kA2,0;1,8,2,1000,3,15,57,5;1,1268,2,10,51,7;1,1268,2,20,51,7;1,901,2,0,51,5,28,-300,10,0,57,7,62,1;'
        sim=load_simulator(raw);engine=sim.level.engine
        self.assertEqual(len(engine._always),1)
        timeline=engine.timeline(((0,0,1.),))
        objects=timeline.objects_near(30,390,410)
        self.assertEqual(len(objects),1)
        self.assertEqual(objects[0].x,400)

    def test_dynamic_group_decimal_and_stop_uses_first_stop(self):
        self.assertEqual(dynamic_groups([{'1':'901','51':'7.0'}]),{7})
        triggers,_=parse_triggers([{'1':'901','2':'0','51':'5','28':'100','10':'1'},
                                  {'1':'1616','2':'60','51':'5'}, {'1':'1616','2':'120','51':'5'}])
        timeline=TriggerEngine(triggers,[],{5}).timeline(((0,0.,1.),))
        self.assertEqual(timeline.moves[5][0].stop,60)

    def test_unimplemented_trigger_variants_are_not_silently_approximated(self):
        for raw in ['kA2,0;1,901,2,10,51,5,59,1;', 'kA2,0;1,901,2,10,51,5,11,1;',
                    'kA2,0;1,901,2,10,51,5,28,nan;', 'kA2,0;1,1346,2,10,51,5;',
                    'kA2,0;1,901,2,10,51,5,85,-100;', 'kA2,0;1,901,2,10,51,5,30,19;',
                    'kA2,0;1,1268,2,10,51,5,63,-10;']:
            with self.assertRaises(UnsupportedLevel): load_simulator(raw)

    def test_nonfinite_teleport_parameters_rejected_before_level_construction(self):
        from unittest.mock import patch
        import predictor
        for key in ('346', '348', '349', '350', '354'):
            for value in ('nan', 'inf', '-inf'):
                with self.subTest(key=key, value=value), patch.object(predictor, 'build_level') as build:
                    with self.assertRaises(UnsupportedLevel):
                        load_simulator(f'kA2,0;1,747,2,100,3,45,345,1,{key},{value};')
                    build.assert_not_called()
        # The coordinate ceiling is not a force limit.
        sim=load_simulator('kA2,0;1,747,2,100,3,45,345,1,346,10000001;')
        self.assertFalse(sim.view(sim.initial_state())['dead'])
