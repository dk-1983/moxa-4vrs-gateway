"""Offline shell tests. All time and power commands are replaced by stubs."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('migration', root / 'tools/prepare-clock-writer-migration.py')
migration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(migration)
with tempfile.TemporaryDirectory() as directory:
    base = Path(directory)
    source = base / 'captures'
    source.mkdir()
    for name in ('ntpdate', 'ntpdate.d'):
        (source / ('vendor-' + name)).write_text('#!/bin/sh\ncase "$1" in\nstart)\n  echo NTP_STUB\n;;\nesac\n')
    (source / 'vendor-halt').write_text('#!/bin/sh\nhwclock --systohc\nhalt -d -f -i -p\n')
    destination = base / 'prepared'
    migration.prepare(source, destination)
    marker = base / 'managed'
    for enabled in (False, True):
        if enabled:
            marker.touch()
        for name in ('ntpdate', 'ntpdate.d', 'halt'):
            text = (destination / name).read_text().replace(migration.MARKER, str(marker))
            text = text.replace('hwclock --systohc', 'echo RTC_STUB').replace('halt -d -f -i -p', 'echo HALT_STUB')
            subprocess.run(['sh', '-n'], input=text, text=True, check=True)
            output = subprocess.check_output(['sh', '-s', 'start'], input=text, text=True)
            if name == 'halt':
                assert 'HALT_STUB' in output
                assert ('RTC_STUB' in output) == (not enabled)
            else:
                assert ('NTP_STUB' in output) == (not enabled)
    try:
        migration.prepare(source, destination)
    except FileExistsError:
        pass
    else:
        raise AssertionError('must not overwrite prepared material')
print('clock writer migration: 6 shell cases PASS; no-overwrite PASS; clock/power commands stubbed')
