#!/usr/bin/env python3
"""Native regression for the pinned dependency; no device or private image needed."""
from pathlib import Path
import argparse
import json
import sys
import subprocess
import tempfile
from patch_littlefs_mount import patched_source
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=ROOT/'.cache/tls-toolchain/builder/managed_components/joltwallet__littlefs/src/littlefs')
args = parser.parse_args()
source = (args.source/'lfs.c').read_bytes()
try:
    patched_source(source + b'\n')
except RuntimeError:
    pass
else:
    raise RuntimeError('Patch must reject an unrecognized source version')
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp/'lfs.c').write_bytes(patched_source(source))
    # CMake source properties are directory-scoped: prove the replacement
    # retains the SDK config header on a target created in a child directory.
    project = tmp/'cmake-probe'
    component = project/'managed_components/joltwallet__littlefs'
    (component/'src/littlefs').mkdir(parents=True)
    (component/'src/littlefs/lfs.c').write_bytes(source)
    (component/'CMakeLists.txt').write_text(
        'add_library(__idf_joltwallet__littlefs STATIC "${CMAKE_CURRENT_SOURCE_DIR}/src/littlefs/lfs.c")\n'
        'set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/src/littlefs/lfs.c" '
        'PROPERTIES COMPILE_FLAGS "-DLFS_CONFIG=lfs_config.h")\n')
    (project/'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.18)\nproject(mount_guard C)\n'
        'set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n'
        f'set(PYTHON "{sys.executable}")\n'
        'add_subdirectory(managed_components/joltwallet__littlefs)\n'
        f'include("{ROOT / "scripts/littlefs_mount_guard.cmake"}")\n')
    subprocess.run(['cmake','-S',str(project),'-B',str(project/'build')],
                   check=True,stdout=subprocess.DEVNULL)
    commands=json.loads((project/'build/compile_commands.json').read_text())
    assert len(commands)==1 and 'esp32base-littlefs/lfs.c' in commands[0]['file']
    assert '-DLFS_CONFIG=lfs_config.h' in commands[0]['command']
    print('CMake replacement preserves component config header')
    binary=tmp/'mount-test'
    subprocess.run(['cc','-g','-fsanitize=undefined','-fno-sanitize-recover=all','-DLFS_NO_ASSERT','-I',str(tmp),'-I',str(args.source),str(ROOT/'scripts/tests/littlefs_mount_guard.c'),str(args.source/'lfs_util.c'),'-o',str(binary)],check=True)
    for case in ('valid','blank','zeros','missing','missing-fixed','zero','one'):
        subprocess.run([str(binary),case],check=True)
    # Prove this test distinguishes the original bug from the patched behavior.
    (tmp/'lfs.c').write_bytes(source)
    subprocess.run(['cc','-g','-fsanitize=undefined','-fno-sanitize-recover=all','-DLFS_NO_ASSERT','-I',str(tmp),'-I',str(args.source),str(ROOT/'scripts/tests/littlefs_mount_guard.c'),str(args.source/'lfs_util.c'),'-o',str(binary)],check=True)
    result=subprocess.run([str(binary),'missing'],capture_output=True,text=True)
    assert result.returncode and 'division by zero' in result.stderr, result
    print('Unpatched missing-superblock case reproduces division by zero')
