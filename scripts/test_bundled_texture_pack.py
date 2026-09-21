#!/usr/bin/env python3
"""Exercise the actual native pack lifecycle in a disposable profile."""
from pathlib import Path
import subprocess
import sys
import tempfile
from prepare_srgu_texture_pack import native_alias, verify

executable = Path(sys.argv[1]).resolve()
pack = executable.parent / 'assets/texture-packs/srgu-remastered'
# Independent known vector from the native Rice policy regression test.
assert native_alias('37b491a3#0#2') == 'a5998f33f8411ab1'
verify(pack)
with tempfile.TemporaryDirectory(prefix='dkr-bundled-pack-') as temporary:
    subprocess.run([str(executable), '--self-test-bundled-pack',
                    str(Path(temporary) / 'profile')], cwd=temporary, check=True)
verify(pack)
