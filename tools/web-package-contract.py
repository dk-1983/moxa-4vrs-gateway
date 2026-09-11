"""Executable format/recovery SPECIFICATION, not an installer or packager.

Models a fixed seven-payload (UI and Help embedded in Web ELF) format 3 for review and regression fixtures.
No filesystem writes, service control, or extraction is implemented here.
"""
import re
NAMES = ('4vrs-install', '4vrs-gateway', '4vrs-gateway.init',
         '4vrs-networking-wrapper', '4vrs-web', '4vrs-rng', '4vrs-kdf')

def validate(manifest):
    if set(manifest) != {'format','product','version','entrypoint','files'}:
        raise ValueError('unexpected manifest fields')
    if (type(manifest['format']) is not int or manifest['format']!=3
        or manifest['product']!='4VRS Gateway' or manifest['version']!='v2026.02.01'
        or manifest['entrypoint']!='4vrs-install'):
        raise ValueError('incompatible envelope')
    files=manifest['files']
    if not isinstance(files,list) or len(files)!=len(NAMES):raise ValueError('payload count')
    for entry,name in zip(files,NAMES):
        if set(entry)!={'name','size','mode','sha256'} or entry['name']!=name:raise ValueError('fixed payload order/name')
        maximum=65536 if name in NAMES[2:4] else 8*1024*1024
        if type(entry['size']) is not int or not 1<=entry['size']<=maximum:raise ValueError('size')
        if entry['mode']!='0755':raise ValueError('mode')
        if not isinstance(entry['sha256'],str) or not re.fullmatch('[a-f0-9]{64}',entry['sha256']):raise ValueError('digest')
    return True

def recovery(phase, previous, candidate, healthy=False):
    """Select an ENTIRE generation. Never mix settings, key/cert and binaries.
    Generation references release assets and the configuration before-image.
    Security files stay in their private directory and are never restored from
    the installer journal; installation does not write them.
    An undecided interrupted update restores previous, even after activation.
    """
    if phase not in ('prepared','activated','committed'):raise ValueError('phase')
    if phase=='committed':return candidate, ()
    if previous==candidate and healthy:return previous, ()
    return previous, ('restore-generation','reconcile-saved-web-state')

def upgrade(previous_version, saved_enabled=None, certificate_identity=None):
    if previous_version not in ('v2026.01.01','v2026.02.01'):raise ValueError('unsupported source')
    if saved_enabled not in (None,False,True):raise ValueError('state')
    return {'enabled': True if saved_enabled is None else saved_enabled,
            'certificate_identity':certificate_identity,
            'generate_certificate':False} # first authorized enable owns key generation
