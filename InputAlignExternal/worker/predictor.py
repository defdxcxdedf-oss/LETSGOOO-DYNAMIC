"""Independent prediction. No game memory, injection, or live GD objects are used here."""
from __future__ import annotations

from pathlib import Path
import base64
import ctypes
import math
import os
import sys
import time
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent / 'vendor'))
from gdgen.levelstring import parse_level
from gdgen.objects import load_objects
from gdgen.sim.fast import FastSimulator
from gdgen.sim.level import build_level, make_phys_object, COLLIDABLE, DYNAMIC_TRIGGER_IDS, BUCKET
from protocol import HELD, VALID, HEADER, DECISION, DECISION_BODY, matches, state_tuple, decision


class UnsupportedLevel(ValueError):
    pass


VISUAL_TRIGGER_IDS = frozenset({29, 30, 105, 899, 900, 915, 1006, 1007})
SUPPORTED_TRIGGER_IDS = VISUAL_TRIGGER_IDS | {901, 1049, 1268, 1616}
MAX_INDEX_ENTRIES = 200000
MAX_COORDINATE = 10_000_000


def load_simulator(text: str):
    if not os.environ.get('GDGEN_SIM_LIB'):
        raise RuntimeError('Compila el proyecto con CMake y define GDGEN_SIM_LIB; la consola necesita su DLL completa')
    maximum = 32 * 1024 * 1024
    if len(text) > maximum:
        raise UnsupportedLevel('Nivel demasiado grande')
    text = text.strip()
    if ';' not in text and ',' not in text:
        try:
            compressed = base64.urlsafe_b64decode(text + '=' * (-len(text) % 4))
            decoder = zlib.decompressobj(15 | 32)
            raw = decoder.decompress(compressed, maximum + 1)
        except (ValueError, zlib.error) as error:
            raise UnsupportedLevel('Nivel comprimido invalido') from error
        if len(raw) > maximum or not decoder.eof or decoder.unconsumed_tail:
            raise UnsupportedLevel('Nivel comprimido invalido o demasiado grande')
        text = raw.decode('utf-8')
    try:
        header, objects = parse_level(text, max_objects=250000)
    except ValueError as error:
        raise UnsupportedLevel(str(error)) from error
    # Do not silently drop geometry/triggers as the upstream path-search tool may do.
    if header.get('kA8') == '1' or header.get('kA10') == '1':
        raise UnsupportedLevel('Dual o dos jugadores: correccion suspendida')
    table = load_objects()
    coverage = 0
    for obj in objects:
        ident = int(obj.get('1', '0'))
        info = table.get(ident)
        if info is None:
            raise UnsupportedLevel(f'Objeto desconocido {ident}: correccion suspendida')
        # Accept only triggers implemented by the external event engine.
        if ident in (DYNAMIC_TRIGGER_IDS - SUPPORTED_TRIGGER_IDS) or (info.category == 'trigger' and ident not in SUPPORTED_TRIGGER_IDS):
            raise UnsupportedLevel(f'Trigger dinamico {ident}: correccion suspendida')
        if info.type in {'TeleportOrb', 'CustomRing',
                         'NormalMirrorPortal', 'InverseMirrorPortal'}:
            raise UnsupportedLevel(f'{info.type}: correccion suspendida')
        # Spawn/touch triggers, object links and gameplay rotation can alter static geometry.
        if obj.get('11') == '1' or obj.get('171') == '1':
            raise UnsupportedLevel('Trigger activable: correccion suspendida')
        try:
            for key in ('2', '3', '6', '32', '128', '129', '10', '28', '29', '30', '51', '54', '63', '85', '143'):
                if key in obj:
                    value = float(obj[key])
                    if not math.isfinite(value) or (key not in ('32', '128', '129') and abs(value) > MAX_COORDINATE):
                        raise UnsupportedLevel('Geometria no finita o fuera de limites')
            if info.type == 'TeleportPortal':
                # Force, limits and multiplier are not coordinates: require finite values separately.
                for key in ('346', '348', '349', '350', '354'):
                    if key in obj and not math.isfinite(float(obj[key])):
                        raise UnsupportedLevel('Parametro de teletransporte no finito')
            if ident == 901:
                duration, easing, rate = float(obj.get('10', 0)), float(obj.get('30', 0)), float(obj.get('85', 2))
                if not (0 <= duration <= 3600 and easing.is_integer() and 0 <= easing <= 18 and .01 <= rate <= 20):
                    raise UnsupportedLevel('Parametros Move fuera del modelo')
            if ident == 1268 and not 0 <= float(obj.get('63', 0)) <= 3600:
                raise UnsupportedLevel('Retardo Spawn fuera del modelo')
            if info.category in COLLIDABLE and obj.get('121') != '1':
                geometry = make_phys_object(0, obj, info)
                bounds = (geometry.x, geometry.y, geometry.x0, geometry.y0, geometry.x1, geometry.y1)
                if any(not math.isfinite(v) or abs(v) > MAX_COORDINATE for v in bounds):
                    raise UnsupportedLevel('Geometria fuera de limites')
                coverage += math.floor(geometry.x1 / BUCKET) - math.floor(geometry.x0 / BUCKET) + 1
                if coverage > MAX_INDEX_ENTRIES:
                    raise UnsupportedLevel('Indice de colisiones demasiado grande')
        except (ValueError, OverflowError) as error:
            raise UnsupportedLevel(f'Geometria no compatible: {error}') from error
    level = build_level(header, objects, triggers=True)
    if level.engine.approximations:
        raise UnsupportedLevel('Mecanica de trigger sin representar: ' + ', '.join(level.engine.approximations))
    if level.dynamic_skipped or level.start_dual:
        raise UnsupportedLevel('Geometria no representable')
    return FastSimulator(level)


class Predictor:
    def __init__(self, sim):
        self.sim = sim
        self.state = sim.initial_state()
        self.tick = 0
        self.session = None
        self.rejected = False
        version = getattr(sim._lib, 'iax_adapter_version', None)
        if version is None or version() != 4:
            raise RuntimeError('Falta adaptador v4: compila worker/branch.cpp con CMake o copia completa InputAlignWorker')
        self.native = getattr(sim._lib, 'iax_predict_v2', None)
        self.clear_preview = getattr(sim._lib, 'iax_clear_preview_notes', None)
        if self.native is None or self.clear_preview is None:
            raise RuntimeError('DLL incompleta: copia completa la nueva carpeta InputAlignWorker')
        self.clear_preview.argtypes, self.clear_preview.restype = [ctypes.c_void_p], None
        if self.native:
            self.native.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                                    ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_void_p]
            self.native.restype = ctypes.c_int

    def synchronize(self, header, actual, history, count):
        if header.session != self.session:
            if self.sim.unsupported:
                # Notes are cumulative inside the dependency; they belong to one attempt, not the next.
                self.sim = FastSimulator(self.sim.level)
            self.state = self.sim.initial_state()
            self.tick, self.session, self.rejected = 0, header.session, False
        if self.rejected:
            raise ValueError('Estado divergente; reinicia el intento')
        if header.tick < self.tick:
            raise ValueError('Paquete antiguo')
        if header.tick - self.tick > count:
            self.rejected = True
            raise ValueError('Falta historial; reinicia el intento')
        while self.tick < header.tick:
            held = bool(history >> (header.tick - self.tick - 1) & 1)
            self.state = self.sim.step(self.state, held)
            self.tick += 1
        view = self.sim.view(self.state, include_lists=False)
        expected = state_tuple(view)
        # A button may change between physics updates; history applies that edge on the next step.
        physical_actual = (*actual[:4], actual[4] & ~HELD)
        physical_expected = (*expected[:4], expected[4] & ~HELD)
        if self.sim.unsupported or view['dead'] or not matches(physical_actual, physical_expected):
            self.rejected = True
            raise ValueError('Fisica no coincide; correccion suspendida hasta reiniciar')
        return view

    def forecast(self, header, actual, history, count, horizon, budget, options, *, clock=time.perf_counter):
        view = self.synchronize(header, actual, history, count)
        if not actual[4] & VALID or not horizon:
            return None
        if self.native and clock is time.perf_counter:
            buffer = ctypes.create_string_buffer(HEADER.size + DECISION_BODY.size)
            result = self.native(self.sim._h, self.state._h, horizon, budget, 4, options, int(bool(actual[4] & HELD)), buffer)
            if result == 1:
                return header.pack(DECISION, header.tick + 4) + buffer.raw[HEADER.size:]
            if result in (0, 2):
                return None
            if result == 3:
                return None  # Unrepresentable future: keep real-state synchronization alive.
            self.rejected = True
            raise ValueError('Error de simulacion; reinicia el intento')
        try:
            return self._reference_forecast(header, actual, horizon, budget, options, clock)
        finally:
            # synchronize has already rejected real-state notes; remaining notes are hypothetical.
            self.clear_preview(self.sim._h)

    def _reference_forecast(self, header, actual, horizon, budget, options, clock):
        deadline = clock() + budget / 1_000_000
        # Four ticks of lead gives the local worker time to answer without holding the game thread.
        lead = 4
        base = self.state
        held = bool(actual[4] & HELD)
        for _ in range(lead):
            base = self.sim.step(base, held)
        view = self.sim.view(base)
        if view['dead']:
            return None
        target = header.tick + lead
        expected = state_tuple(view)

        def survives(state, action, collect=False):
            points = []
            for i in range(horizon):
                if clock() >= deadline:
                    return None, []
                state = self.sim.step(state, action)
                v = self.sim.view(state)
                if self.sim.unsupported:
                    return None, []
                if v['dead']:
                    return False, []
                if collect and i % max(1, (horizon + 31) // 32) == 0:
                    points.append((v['x'], v['y']))
            return True, points

        immediate, _ = survives(base, not held)
        if immediate is not False:
            return None  # Safe immediate input or incomplete query: preserve the original click.
        delayed = base
        for delay in range(1, horizon + 1):
            if clock() >= deadline:
                return None
            delayed = self.sim.step(delayed, held)
            due = self.sim.view(delayed)
            if due['dead']:
                return None
            safe, points = survives(delayed, not held, bool(options & 1))
            if safe is None:
                return None
            if safe:
                return decision(header, target, expected, state_tuple(due), delay, not held, points)
        return None
