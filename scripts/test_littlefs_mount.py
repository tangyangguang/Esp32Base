#!/usr/bin/env python3
"""Native regression for the pinned dependency; no device or private image needed."""
from pathlib import Path
import argparse
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
