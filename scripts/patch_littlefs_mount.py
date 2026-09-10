#!/usr/bin/env python3
"""Generate a checked LittleFS source copy; never edit managed components."""
import argparse
import hashlib
from pathlib import Path

SOURCE_SHA256 = '41b7aa8d6e1601ef26129399cc2e5988e736c82a4b89e31183be787722aebf07'
ANCHOR = b'    // update littlefs with gstate\n'
GUARD = b'''    // Reject metadata without a usable superblock before allocator arithmetic.
    // A zero configured block_count means autodetect, not a valid empty volume.
    if (lfs_pair_isnull(lfs->root) || lfs->block_count < 2) {
        err = LFS_ERR_CORRUPT;
        goto cleanup;
    }

'''

def patched_source(source: bytes) -> bytes:
    if hashlib.sha256(source).hexdigest() != SOURCE_SHA256:
        raise RuntimeError('Unexpected LittleFS source: review patch against the pinned component')
    if source.count(ANCHOR) != 1:
        raise RuntimeError('LittleFS mount anchor changed')
    return source.replace(ANCHOR, GUARD + ANCHOR)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = patched_source(args.source.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_bytes() != result:
        args.output.write_bytes(result)
