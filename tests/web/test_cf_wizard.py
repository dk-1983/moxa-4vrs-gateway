"""Real ext3/MBR file images + injected mount boundary; public RNG only, no loop/physical device."""
from pathlib import Path
import copy
import hashlib
import importlib.util
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('wizard', ROOT/'tools/cf-wizard.py')
w = importlib.util.module_from_spec(spec); spec.loader.exec_module(w)
BIN = Path(sys.argv.pop(1)).resolve()
WORKER = sys.argv.pop(1) if len(sys.argv) > 1 else 'wizard-fixture'
CHOSEN = w.target('Fixture Moxa 2', '10.0.2.15:22', '02:00:00:00:00:22')  # Synthetic MAC, not device identity.


class Images:
    def __init__(self, path, blank=False):
        self.path = path
        self.disk = path/'disk.img'; self.part = path/'part.img'; self.tree = path/'tree'
        self.tree.mkdir(mode=0o755)
        with self.disk.open('wb') as f: f.truncate(64*1024*1024)
        self.events = []; self.failure = None; self.env = {}; self.fds = []; self.mounted = False
        self.doc = dict(disk=str(self.disk), dev='fixture-only', sysfs='/fixture', reader='14cd:168a',
                        serial='816820130806', disk_bytes=self.disk.stat().st_size, partitions=[], blocked=None)
        if not blank: self.make_fs()
        self.doc['id'] = w.identity(self.doc)

    def make_fs(self):
        w.command(['sfdisk', self.disk], input=w.PARTITION_RECIPE)
        table = json.loads(w.command(['sfdisk', '--json', self.disk]))['partitiontable']['partitions'][0]
        self.offset = table['start']*512
        with self.part.open('wb') as f: f.truncate(table['size']*512)
        w.command(w.mkfs_command(self.part))
        with self.part.open('rb') as f: f.seek(1024); sb = w.bootstrap.superblock(f.read(1024))
        self.doc['partitions'] = [dict(size=self.part.stat().st_size, uuid=sb['uuid'], fstype='ext3', start=2048)]
        self.doc['id'] = w.identity(self.doc)

    def event(self, name):
        self.events.append(name)
        if self.failure == name: raise KeyboardInterrupt()

    def preflight(self, doc):
        self.event('preflight')
        if not doc['partitions']: return None
        d = os.open(self.disk, os.O_RDONLY); p = os.open(self.part, os.O_RDONLY); self.fds += [d,p]
        sb = w.inspect_headers(d,p,self.part.stat().st_size,self.offset,self.doc['partitions'][0]['uuid'])
        w.command(['e2fsck','-f','-n',self.part])
        return dict(dfd=d,pfd=p,uuid=sb['uuid'],partition_bytes=self.part.stat().st_size)

    def mount(self, doc, media, write=False):
        self.event('mount-rw' if write else 'mount-ro')
        self.mounted = True
        fd = os.open(self.tree,os.O_RDONLY|os.O_DIRECTORY); self.fds.append(fd); return fd

    def inventory(self, root):
        self.event('inventory'); return w.Linux().inventory(root)

    def unmount(self):
        self.event('unmount'); self.mounted = False

    def worker(self, mode, doc, media, root, chosen):
        self.event(mode)
        result = subprocess.run([BIN/WORKER,mode,str(root),str(media['pfd']),str(media['dfd']),chosen['mac'],
                                 media['uuid'],str(doc['disk_bytes']),str(media['partition_bytes']),'confirmed-target'],
                                pass_fds=(root,media['pfd'],media['dfd']),capture_output=True,env={**os.environ,**self.env})
        public = bytes(255 if i == 5 else i for i in range(32))
        assert public not in result.stdout+result.stderr and public.hex().encode() not in result.stdout+result.stderr
        w.require(result.returncode == 0, 'worker refused')

    def format(self, doc):
        self.event('format')
        self.tree.rename(self.path/'erased-tree')
        self.tree.mkdir(mode=0o755)
        self.make_fs(); return self.doc

    def close(self):
        self.events.append('close'); self.mounted = False
        for fd in self.fds: os.close(fd)
        self.fds = []


class WizardTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='cf-wizard-public-'); self.path = Path(self.tmp.name)
    def tearDown(self): self.tmp.cleanup()
    def run_flow(self, b, action='update', chosen=CHOSEN, confirm=True):
        return w.execute(b,b.doc,chosen,action,
                         ask=lambda _:w.confirmation('ERASE-ALL-DATA' if action=='wipe' else 'UPDATE', b.doc) if confirm else 'wrong card',
                         show=lambda _:None, files=self.files)

    @classmethod
    def setUpClass(cls):
        cls.files=w.package.load(ROOT/'build/web-close-candidate-20260910/4vrs-gateway-v2026.02.01-web-close-candidate.tar.gz')

    def installed(self, b):
        media=b.preflight(b.doc);root=b.mount(b.doc,media,True)
        b.worker('prepare',b.doc,media,root,CHOSEN);b.close();b.events=[]
        (b.tree/'4vrs/bin').mkdir(parents=True)
        (b.tree/'4vrs/bin/4vrs-gateway').write_bytes(self.files['4vrs-gateway'][0])
        for name in ['config','admin','tls']:
            (b.tree/'4vrs'/name).mkdir()
            (b.tree/'4vrs'/name/'public-test-sentinel').write_bytes(b'public fixture preserved')
        (b.tree/'4vrs/config/gateway.conf').write_bytes(b'4VRS_GATEWAY_CONFIG\nschema=3\npublic fixture header only\n')

    def test_future_schema_rejected_without_write(self):
        b=Images(self.path);self.installed(b)
        (b.tree/'4vrs/config/gateway.conf').write_bytes(b'4VRS_GATEWAY_CONFIG\nschema=4\n')
        with self.assertRaises(ValueError):self.run_flow(b)
        self.assertNotIn('mount-rw',b.events)

    def test_partition_parentage_and_reidentification_before_write(self):
        live=w.Linux()
        doc=dict(disk='/dev/test',disk_bytes=4009549824,dev='8:16',sysfs='/sys/fixture/disk',
                 partitions=[dict(name='/dev/test1',size=4008501248,**{'maj:min':'8:17'})])
        def opened(path,size):
            fd=100+len(live.fds);live.fds.append(fd);return fd
        with patch.object(live,'recheck') as recheck,patch.object(live,'open_block',side_effect=opened), \
             patch.object(w.os,'fstat',return_value=SimpleNamespace(st_rdev=os.makedev(8,16))), \
             patch.object(Path,'resolve',return_value=Path('/sys/foreign/disk/part')),patch.object(w.os,'close') as close:
            with self.assertRaisesRegex(w.Refused,'topology'):live.write_topology(doc)
            close.assert_called_once_with(100);self.assertEqual(live.fds,[])
        with patch.object(live,'recheck') as recheck,patch.object(live,'open_block',side_effect=opened), \
             patch.object(w.os,'fstat',side_effect=[SimpleNamespace(st_rdev=os.makedev(8,16)),SimpleNamespace(st_rdev=os.makedev(8,17))]), \
             patch.object(Path,'resolve',return_value=Path('/sys/fixture/disk/part')), \
             patch.object(Path,'read_text',return_value='2048'),patch.object(w.os,'close') as close:
            live.write_topology(doc)
            self.assertEqual(recheck.call_count,2);self.assertEqual(close.call_count,2);self.assertEqual(live.fds,[])

    def test_media_replaced_before_format_command(self):
        live=w.Linux()
        with patch.object(live,'write_topology'),patch.object(live,'recheck',side_effect=w.Refused('changed')),patch.object(w,'command') as cmd:
            with self.assertRaises(w.Refused):live.format({})
            cmd.assert_not_called()

    def test_blank_and_used_wipe(self):
        for blank in [True,False]:
            folder=self.path/str(blank);folder.mkdir();b=Images(folder,blank)
            (b.tree/'old-data').write_bytes(b'public old data')
            r=self.run_flow(b,'wipe')
            self.assertEqual(b.events[0],'format');self.assertFalse(r['commissioned'])
            self.assertFalse((b.tree/'old-data').exists());self.assertFalse(b.mounted)
            self.assertEqual(set((b.tree/r['package_path']).iterdir()),{b.tree/r['package_path']/n for n in self.files})
            self.assertTrue((b.tree/'4vrs-rng/state').exists())

    def test_update_preserves_all_existing_bytes_and_no_rng_generation(self):
        b=Images(self.path);self.installed(b)
        before={p.relative_to(b.tree):p.read_bytes() for p in b.tree.rglob('*') if p.is_file()}
        b.env={'CF_RANDOM':'forbidden'}
        r=self.run_flow(b)
        for p,data in before.items():self.assertEqual((b.tree/p).read_bytes(),data)
        self.assertNotIn('prepare',b.events);self.assertNotIn('format',b.events)
        self.assertFalse(r['commissioned']);self.run_flow(b) # Verified idempotent staging.

    def test_old_rng_dirty_wipe_but_update_refused(self):
        b=Images(self.path);self.installed(b);old=(b.tree/'4vrs-rng/state').read_bytes()
        with b.part.open('r+b') as f:f.seek(1120);f.write(struct.pack('<I',6))
        before=hashlib.sha256(b.part.read_bytes()).digest()
        with self.assertRaisesRegex(w.Refused,'needs_recovery'):self.run_flow(b)
        self.assertEqual(before,hashlib.sha256(b.part.read_bytes()).digest())
        b.events=[];r=self.run_flow(b,'wipe')
        self.assertEqual(b.events[0],'format');self.assertNotEqual(old,(b.tree/'4vrs-rng/state').read_bytes())

    def test_empty_update_and_missing_rng_refused(self):
        for blank in [True,False]:
            folder=self.path/str(blank);folder.mkdir();b=Images(folder,blank)
            with self.assertRaises(w.Refused):self.run_flow(b)
            self.assertNotIn('mount-rw',b.events)

    def test_wrong_confirmation_and_protected_disk_never_write(self):
        b=Images(self.path)
        with self.assertRaises(w.Refused):self.run_flow(b,'wipe',confirm=False)
        self.assertEqual(b.events,['close'])
        b.doc['blocked']='System disk'
        with self.assertRaises(w.Refused):self.run_flow(b,'wipe')
        self.assertNotIn('format',b.events)

    def test_wrong_target_update_refused(self):
        b=Images(self.path);self.installed(b)
        with self.assertRaises(w.Refused):self.run_flow(b,chosen=w.target('Other','192.0.2.1:22','02:00:00:00:00:23'))
        self.assertNotIn('mount-rw',b.events)

    def test_incomplete_rng_not_reseeded_by_update(self):
        b=Images(self.path);b.env={'CF_CRASH':'written'}
        with self.assertRaises(w.Refused):self.run_flow(b,'wipe')
        before=(b.tree/'4vrs-rng/pending').read_bytes();b.env={'CF_RANDOM':'forbidden'}
        with self.assertRaises(w.Refused):self.run_flow(b)
        self.assertEqual(before,(b.tree/'4vrs-rng/pending').read_bytes())

    def test_interrupted_staging_preserves_active_and_blocks_retry(self):
        b=Images(self.path);self.installed(b);old=(b.tree/'4vrs/bin/4vrs-gateway').read_bytes()
        original=w.package.os.fsync;calls=[]
        def crash(fd):
            calls.append(fd)
            if len(calls)==5:raise KeyboardInterrupt()
            original(fd)
        with patch.object(w.package.os,'fsync',side_effect=crash):
            with self.assertRaises(KeyboardInterrupt):self.run_flow(b)
        self.assertFalse(b.mounted);self.assertEqual(old,(b.tree/'4vrs/bin/4vrs-gateway').read_bytes())
        self.assertTrue(any(p.name.endswith('.pending') for p in (b.tree/'4vrs-packages').iterdir()))
        with self.assertRaises(ValueError):self.run_flow(b)

    def test_interrupted_mount_worker_unmount(self):
        for stage in ['format','preflight','mount-rw','prepare','verify','unmount','mount-ro']:
            folder=self.path/stage;folder.mkdir();b=Images(folder);b.failure=stage
            with self.assertRaises(KeyboardInterrupt):self.run_flow(b,'wipe')
            self.assertFalse(b.mounted);self.assertEqual(b.events[-1],'close')

    def test_symlink_staging_rejected(self):
        b=Images(self.path);self.installed(b)
        outside=self.path/'outside';outside.mkdir();(b.tree/'4vrs-packages').symlink_to(outside)
        with self.assertRaises(OSError):self.run_flow(b)
        self.assertEqual(list(outside.iterdir()),[])

    def test_staged_tamper_refused(self):
        b=Images(self.path);self.installed(b);r=self.run_flow(b)
        (b.tree/r['package_path']/'4vrs-web').write_bytes(b'bad')
        with self.assertRaises(ValueError):self.run_flow(b)

    def test_full_package_roundtrip_through_ext3_image(self):
        b=Images(self.path);r=self.run_flow(b,'wipe')
        commands=['mkdir /4vrs-packages','mkdir /'+r['package_path'],'mkdir /4vrs-rng']
        paths=[r['package_path']+'/'+n for n in self.files]+['4vrs-rng/state']
        for name in paths:commands.append('write '+str(b.tree/name)+' /'+name)
        script=self.path/'public-debugfs.commands';script.write_text('\n'.join(commands)+'\n')
        w.command(['debugfs','-w','-f',script,b.part])
        for i,name in enumerate(paths):
            restored=self.path/('restored-'+str(i))
            w.command(['debugfs','-R','dump /'+name+' '+str(restored),b.part])
            self.assertEqual(restored.read_bytes(),(b.tree/name).read_bytes())
        w.command(['e2fsck','-f','-n',b.part])

    def test_named_card_geometry_dirty_sparse_image(self):
        b=Images(self.path,blank=True)
        with b.disk.open('r+b') as f:f.truncate(4009549824)
        b.doc['disk_bytes']=4009549824
        b.make_fs()
        self.assertEqual(b.part.stat().st_size,4008501248)
        args=w.mkfs_command(b.part);args[-1:-1]=['-U','d5577b6d-e75a-dc47-9322-aca3485ef58d']
        w.command(args)
        b.doc['partitions'][0]['uuid']='d5577b6de75adc479322aca3485ef58d';b.doc['id']=w.identity(b.doc)
        with b.part.open('r+b') as f:f.seek(1120);f.write(struct.pack('<I',6))
        with self.assertRaisesRegex(w.Refused,'needs_recovery'):self.run_flow(b)
        self.assertEqual(b.events,['preflight','close'])
        self.assertFalse((b.tree/'4vrs-rng').exists())

    def test_real_format_commands_on_image_with_injected_discovery(self):
        b=Images(self.path,blank=True);live=w.Linux()
        def scan():
            with b.disk.open('rb') as f:f.seek(454);start,count=struct.unpack('<II',f.read(8))
            if count:
                if not b.part.exists():
                    with b.part.open('wb') as f:f.truncate(count*512)
                uuid=None;kind=None
                with b.part.open('rb') as f:f.seek(1024);data=f.read(1024)
                if data[56:58]==b'\x53\xef':uuid=w.bootstrap.superblock(data)['uuid'];kind='ext3'
                b.doc['partitions']=[dict(name=str(b.part),size=count*512,start=start,uuid=uuid,fstype=kind)]
            b.doc['id']=w.identity(b.doc)
            return [copy.deepcopy(b.doc)]
        live.scan=scan
        live.write_topology=lambda doc: live.recheck(doc) # Block/sysfs boundary injected for files.
        # udev is the other injected boundary: regular files have no partition nodes.
        original=w.command
        def cmd(args,**kwargs):
            if args[0]=='udevadm':return b''
            return original(args,**kwargs)
        with patch.object(w,'command',side_effect=cmd):result=live.format(copy.deepcopy(b.doc))
        self.assertEqual(result['partitions'][0]['fstype'],'ext3')
        self.assertIsNotNone(result['partitions'][0]['uuid'])

    def test_system_disks_swap_and_mappers_protected(self):
        node=dict(type='disk',tran='usb',size=4009549824,ro=False,mountpoints=[],children=[])
        self.assertIsNone(w.disk_policy(node))
        for mount in ['/','/boot','/home','/boot/efi','[SWAP]','/media/automounted-card']:
            n=copy.deepcopy(node);n['children']=[dict(type='part',ro=False,mountpoints=[mount])]
            self.assertIsNotNone(w.disk_policy(n))
        for kind in ['crypt','lvm','raid1','loop']:
            n=copy.deepcopy(node);n['children']=[dict(type=kind,ro=False,mountpoints=[])]
            self.assertIsNotNone(w.disk_policy(n))
        node['tran']='sata';self.assertIsNotNone(w.disk_policy(node))

    def test_identity_changes_and_disk_numbers_are_not_identity(self):
        b=Images(self.path);d=copy.deepcopy(b.doc);d['disk']='/dev/different-number'
        self.assertEqual(w.identity(d),b.doc['id'])
        for key,value in [('serial','other-reader'),('disk_bytes',123456789),('reader','ffff:ffff')]:
            d=copy.deepcopy(b.doc);d[key]=value;self.assertNotEqual(w.identity(d),b.doc['id'])
        d=copy.deepcopy(b.doc);d['partitions'][0]['uuid']='f'*32;self.assertNotEqual(w.identity(d),b.doc['id'])
        live=w.Linux();live.scan=lambda:[d]
        with self.assertRaises(w.Refused):live.recheck(b.doc)

    def test_target_validation(self):
        self.assertEqual(CHOSEN['address'],'10.0.2.15:22')
        for mac in ['00:00:00:00:00:00','ff:ff:ff:ff:ff:ff','01:00:00:00:00:00','bad;command']:
            with self.assertRaises(w.Refused):w.target('Moxa','10.0.2.15:22',mac)
        for address in ['hostname:22','10.0.2.15','10.0.2.15:0','10.0.2.15:99999']:
            with self.assertRaises(w.Refused):w.target('Moxa',address,CHOSEN['mac'])

    def test_readable_inventory_and_block_reason(self):
        b=Images(self.path);b.doc['blocked']='Mounted: protected';lines=[]
        w.display_disks([b.doc],lines.append)
        text='\n'.join(lines)
        self.assertIn('GiB',text);self.assertIn('816820130806',text);self.assertIn('BLOCKED',text)
        self.assertIn(b.doc['partitions'][0]['uuid'],text)

    def test_direct_uuid_probe_replaces_stale_udev_cache(self):
        p=dict(name='/dev/current-partition',uuid='stale',fstype='ext4')
        with patch.object(w,'command',return_value=b'UUID=00112233-4455-6677-8899-aabbccddeeff\nTYPE=ext3\n') as cmd:
            w.refresh_partition(p)
            self.assertEqual(cmd.call_args.args[0],['blkid','-p','-o','export','/dev/current-partition'])
        self.assertEqual(p['fstype'],'ext3');self.assertNotEqual(p['uuid'],'stale')
        with patch.object(w,'command',return_value=b''):
            w.refresh_partition(p)
        self.assertIsNone(p['fstype']);self.assertIsNone(p['uuid'])

    def test_production_worker_rejects_images(self):
        b=Images(self.path);media=b.preflight(b.doc);fd=b.mount(b.doc,media)
        try:
            r=subprocess.run([BIN/'4vrs-cf-rng-worker','prepare',str(fd),str(media['pfd']),str(media['dfd']),
                              CHOSEN['mac'],media['uuid'],str(b.doc['disk_bytes']),str(media['partition_bytes']),'confirmed-target'],
                             pass_fds=(fd,media['pfd'],media['dfd']),capture_output=True)
            self.assertNotEqual(r.returncode,0);self.assertFalse((b.tree/'4vrs-rng').exists())
        finally:b.close()

    def test_mount_failure_cleanup_and_no_forced_unmount(self):
        backend=w.Linux();backend.mountpoint='/run/test';backend.partition_dev='8:17'
        text='40 20 8:17 / /run/test ro,nosuid,nodev,noexec - ext3 /dev/example ro,norecovery\n'
        with patch.object(Path,'read_text',return_value=text),patch.object(w,'command') as cmd:
            backend.unmount();self.assertEqual(cmd.call_args.args[0],['umount','/run/test'])
        with patch.object(Path,'read_text',return_value=text.replace('8:17','8:18')),patch.object(w,'command') as cmd:
            with self.assertRaises(w.Refused):backend.unmount()
            cmd.assert_not_called()


unittest.main(verbosity=2)
