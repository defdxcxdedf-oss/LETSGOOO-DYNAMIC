"""Input Align External console worker; standard-library Python + the bundled native simulator."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import socket
import sys
import time

# A frozen Windows build ships the compiled simulator; end users need no compiler or Python.
bundle = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parent))
native = bundle / ('gdsim.dll' if os.name == 'nt' else 'libgdsim.so')
if native.is_file():
    os.environ['GDGEN_SIM_LIB'] = str(native)

from protocol import Header, HELLO, decode_frame, status
from predictor import Predictor, UnsupportedLevel, load_simulator


def newer_frame(predictor, frame, queued):
    if (queued[0].session, queued[0].tick) < (frame[0].session, frame[0].tick):
        return frame
    if predictor is not None and (not predictor.rejected or predictor.session != frame[0].session):
        known = predictor.tick if predictor.session == queued[0].session else 0
        if queued[0].tick - known > queued[3]:
            # Validate/replay retained history before the newer window discards it.
            predictor.synchronize(*frame[:4])
    return queued


def serve(directory: Path, *, stop=None):
    config = json.loads((directory / 'bridge.json').read_text(encoding='utf-8'))
    if config.get('version') != 1 or not 1 <= config['port'] <= 65535:
        raise ValueError('Configuracion de puente invalida')
    token = (int(config['tokenHi'], 16), int(config['tokenLo'], 16))
    endpoint = ('127.0.0.1', config['port'])
    predictor = None
    failure = None
    last_hello = last_status = 0.
    last_session = None
    last_message = None
    print('Input Align External | GD 2.2081 | No cierres esta ventana.', flush=True)
    print('Reinicia el intento si el nivel ya estaba en marcha.', flush=True)
    # Keep the game responsive under contention. This changes only this worker's priority.
    if os.name == 'nt':
        import ctypes
        kernel = ctypes.windll.kernel32
        kernel.SetPriorityClass(kernel.GetCurrentProcess(), 0x00004000)  # BELOW_NORMAL_PRIORITY_CLASS
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.connect(endpoint)
        sock.settimeout(.05)
        while stop is None or not stop.is_set():
            now = time.monotonic()
            if now - last_hello >= 1:
                sock.send(Header(HELLO, *token).pack())
                last_hello = now
            try:
                packet = sock.recv(2048)
            except socket.timeout:
                continue
            except ConnectionResetError:
                print('El puente se ha cerrado. Abre la consola desde el nuevo nivel.', flush=True)
                return
            try:
                frame = decode_frame(packet, token)
            except (ValueError, OverflowError):
                continue
            header = frame[0]
            if header.session != last_session:
                last_session = header.session
                if predictor is None and failure is None:
                    try:
                        path = directory / 'level.txt'
                        if path.stat().st_size > 32 * 1024 * 1024:
                            raise UnsupportedLevel('Nivel demasiado grande')
                        predictor = Predictor(load_simulator(path.read_text(encoding='utf-8')))
                    except (ValueError, OSError, RuntimeError, OverflowError, AttributeError) as error:
                        failure = str(error)
                print('Intento', header.session, failure or 'simulador preparado', flush=True)
            # Coalesce queued telemetry before spending CPU on a forecast. History reconstructs skipped ticks.
            sock.setblocking(False)
            try:
                for _ in range(64):
                    try:
                        queued = decode_frame(sock.recv(2048), token)
                        frame = newer_frame(predictor, frame, queued)
                    except (ValueError, OverflowError):
                        continue
                    except BlockingIOError:
                        break
            finally:
                sock.settimeout(.05)
            header = frame[0]
            result = None
            message = failure
            query_error = False
            if predictor:
                try:
                    result = predictor.forecast(*frame)
                    message = 'Conectado: prediccion externa' if frame[1][4] & 256 else 'Conectado: modo sin correccion'
                except (ValueError, OverflowError, RuntimeError) as error:
                    message = str(error)
                    query_error = True
            if result:
                sock.send(result)
            if message != last_message or now - last_status >= 1:
                sock.send(status(header, 1 if failure or query_error or predictor and predictor.rejected else 0, message or 'Esperando'))
                last_status = now
                last_message = message


def main():
    parser = argparse.ArgumentParser(description='Simulador local de Input Align External')
    parser.add_argument('--bridge', type=Path, required=True, help='Carpeta de datos del mod')
    args = parser.parse_args()
    try:
        if not os.environ.get('GDGEN_SIM_LIB'):
            raise RuntimeError('Falta gdsim.dll. Copia el contenido completo de InputAlignWorker o compila con CMake.')
        serve(args.bridge.resolve())
    except (OSError, ValueError, RuntimeError) as error:
        print(f'No se pudo iniciar: {error}', file=sys.stderr, flush=True)
        return 1
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
