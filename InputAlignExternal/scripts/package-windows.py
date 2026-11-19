"""Package a compiled bridge + native simulator with the official embeddable Python runtime.

Works on a Windows build host or a Linux cross-build host. The resulting console needs no Python install.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
# Downloaded from the pinned official python.org HTTPS release URL.
PYTHON_RUNTIME_SHA256 = "4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3"


def package(build: Path, runtime: Path, output: Path, *, native: Path | None = None):
    upstream = json.loads((ROOT / 'worker/vendor/UPSTREAM.json').read_text(encoding='utf-8'))
    vendor = ROOT / 'worker/vendor'
    allowed = set(upstream['files']) | {'LICENSE', 'UPSTREAM.json'}
    for file in vendor.rglob('*'):
        if file.is_file() and '__pycache__' not in file.parts and file.suffix != '.pyc':
            if file.relative_to(vendor).as_posix() not in allowed:
                raise ValueError('Unlisted vendor file: ' + str(file))
    for name in upstream['files']:
        if not (ROOT / 'worker/vendor' / name).is_file():
            raise ValueError(f'Missing pinned simulator file: {name}')
        digest = upstream.get('fileSha256', {}).get(name)
        if not digest:
            raise ValueError(f'Missing simulator digest: {name}')
        if hashlib.sha256((ROOT / 'worker/vendor' / name).read_bytes()).hexdigest() != digest:
            raise ValueError(f'Simulator file hash mismatch: {name}')
    mod = list(build.glob('*.geode'))
    if native is None:
        candidates = [p for p in (build / 'gdsim.dll', build / 'Release/gdsim.dll') if p.is_file()]
        if len(candidates) > 1:
            raise ValueError('ambiguous gdsim.dll: specify the tested DLL with --native')
        native = candidates[0] if candidates else None
    elif not native.is_file():
        raise ValueError('Explicit native DLL does not exist')
    if len(mod) != 1 or native is None:
        raise ValueError('Expected one .geode package and gdsim.dll in the build directory')
    members = {mod[0].name: mod[0].read_bytes(), 'README.md': (ROOT / 'README.md').read_bytes(),
               'LICENSE-InputAlignExternal.txt': (ROOT / 'LICENSE').read_bytes(),
               'InputAlignWorker/gdsim.dll': native.read_bytes()}
    if hashlib.sha256(runtime.read_bytes()).hexdigest() != PYTHON_RUNTIME_SHA256:
        raise ValueError('Untrusted Python runtime digest')
    with zipfile.ZipFile(runtime) as source:
        required = {'python.exe', 'python312.dll', 'python312.zip'}
        files = {i.filename for i in source.infolist() if not i.is_dir() and i.file_size > 0}
        if not required <= files:
            raise ValueError('Missing required runtime files: ' + ', '.join(sorted(required - files)))
        seen = set()
        for info in source.infolist():
            if info.is_dir(): continue
            if '/' in info.filename or '\\' in info.filename or info.filename.startswith('.'):
                raise ValueError('Unexpected embedded runtime archive layout')
            key = info.filename.casefold()
            if key in seen or key == 'gdsim.dll':
                raise ValueError('Runtime member collision')
            seen.add(key)
            members['InputAlignWorker/' + info.filename] = source.read(info)
    members['InputAlignWorker/python312._pth'] = b'python312.zip\n.\nvendor\n'
    for path in sorted((ROOT / 'worker').rglob('*')):
        if (not path.is_file() or '__pycache__' in path.parts or 'build' in path.parts
                or path.suffix in {'.so', '.dll', '.pyc'}): continue
        name = 'InputAlignWorker/' + path.relative_to(ROOT / 'worker').as_posix()
        if name.casefold() in {key.casefold() for key in members}:
            raise ValueError('Worker file collides with staged runtime: ' + name)
        members[name] = path.read_bytes()
    members['InputAlignWorker/Iniciar-consola.cmd'] = (
        '@echo off\r\nsetlocal\r\n'
        'if "%~1"=="" (\r\n echo Usa Abrir CMD en el boton IAX del menu de pausa.\r\n pause\r\n exit /b 2\r\n)\r\n'
        '"%~dp0python.exe" -X utf8 "%~dp0main.py" --bridge "%~1"\r\nif errorlevel 1 pause\r\n').encode('ascii')
    members['build-provenance.json'] = json.dumps({
        'schemaVersion': 1, 'generatedBy': 'CodeRabbit Coding Agent', 'experimental': True,
        'gameplayTested': False, 'target': 'Windows x64, GD 2.2081, Geode 5.11.2',
        'pythonRuntimeSource': 'https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip',
        'pythonRuntimeSha256': hashlib.sha256(runtime.read_bytes()).hexdigest(),
        'simulator': json.loads((ROOT / 'worker/vendor/UPSTREAM.json').read_text(encoding='utf-8')),
        'sha256': {name: hashlib.sha256(data).hexdigest() for name, data in sorted(members.items())},
    }, indent=2).encode() + b'\n'
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(members.items()): archive.writestr(name, data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--python-runtime', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--native', type=Path, help='Exact native DLL used by release tests')
    args = parser.parse_args()
    package(args.build, args.python_runtime, args.output, native=args.native)
