#!/usr/bin/env python3
"""Real child-process restoration and deterministic atomic-publication tests."""
import hashlib
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import sys

EXE = Path(sys.argv[1]).resolve()
FIXTURES = Path(sys.argv[2]).resolve()
BASE = Path(os.environ.get('PB_GUIDE_TEST_WORK_ROOT', str(EXE.parent / 'gear-plan-test-runtime'))).resolve()
os.umask(0o077)
BASE.mkdir(mode=0o700, parents=True, exist_ok=True)
ROOT = BASE / ('persistence-' + str(os.getpid()))
ROOT.mkdir(mode=0o700)
receipts = []

def directory(name):
    p = ROOT / name
    p.mkdir(mode=0o700)
    return p

def args(op, store, variant='A', context='normal', fault='none'):
    return [str(EXE), '--child', op, str(store), str(FIXTURES), variant, context, fault]

def run(op, store, variant='A', context='normal', fault='none', code=None):
    result = subprocess.run(args(op, store, variant, context, fault), capture_output=True, text=True, timeout=60 if variant == 'L' else 20)
    assert result.returncode in (0, 2), (result.returncode, result.stdout, result.stderr)
    receipt = json.loads(result.stdout)
    receipts.append(dict(operation=op, variant=variant, context=context, fault=fault, returncode=result.returncode, **receipt))
    partial = ROOT / 'receipts.json'
    partial.write_text(json.dumps(dict(cases=len(receipts), receipts=receipts, complete=False), indent=2) + '\n')
    partial.chmod(0o600)
    if code:
        assert result.returncode == 2 and receipt['code'] == code, receipt
    else:
        assert result.returncode == 0 and receipt['status'] == 'typed_equal', receipt
        assert (receipt['targets'], receipt['evaluations']) == ((100, 9900) if variant == 'L' else (14, 14)), receipt
    operation_tokens = {'write-error': 'temp_write_errno_28', 'file-fsync-error': 'temp_fsync_errno_5', 'rename-error': 'record_rename_errno_5', 'dir-fsync-error': 'directory_fsync_errno_5', 'readback-error': 'record_read_errno_5', 'temp-close-error': 'temp_close_errno_5', 'readback-close-error': 'record_close_errno_5', 'directory-open-error': 'directory_open_errno_5', 'record-open-error': 'record_open_errno_24'}
    if fault in operation_tokens:
        assert operation_tokens[fault] in receipt['detail'] and len(receipt['detail']) < 1024, receipt
    return receipt

def record(store):
    return store / 'gear_plan:42:10000.json'

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def private(path):
    s = path.stat()
    assert s.st_uid == os.getuid()
    if not path.is_dir():
        assert s.st_nlink == 1
    assert s.st_mode & 0o777 == (0o700 if path.is_dir() else 0o600)

store = directory('committed')
a = run('save', store)
private(store)
private(record(store))
b = run('load', store)
assert a['pid'] != b['pid'], 'fresh load reused save process'
old = record(store).read_bytes()
run('save', store, 'B')
assert old != record(store).read_bytes(), 'B did not replace A'
run('load', store, 'B')
# Full policy constants, plus independent snapshot identities, must bind restore.
for context in ('character', 'content', 'graph', 'core', 'playerbots', 'policy', 'policy-version', 'unsupported-policy'):
    before = digest(record(store))
    run('load', store, 'B', context, code='invalid_context')
    assert digest(record(store)) == before
run('save', store, 'B', context='unsupported-policy', code='invalid_context')
run('save', store, 'B', context='policy')
run('load', store, 'B', context='policy')
run('load', store, 'B', code='invalid_context')
run('save', store, 'B')
run('load', store, 'B')
# G independently loads a second valid capsule with a changed compiler revision,
# manifest digest and source generation; graph and plan ID remain the same.
run('load', store, 'G', code='invalid_context')
run('save', store, 'G')
run('load', store, 'G')
run('load', store, 'A', code='invalid_context')
run('save', store, 'A')
run('load', store, 'A')
run('save', store, 'B')
run('load', store, 'B', context='historical')
for context in ('policy161', 'policy2500'):
    run('save', store, 'B', context=context)
    run('load', store, 'B', context=context)
run('save', store, 'B')
large = directory('large-actual-plan')
large_a = run('save', large, 'L')
assert record(large).stat().st_size > 1024 * 1024
assert record(large).stat().st_size < 16 * 1024 * 1024
large_b = run('load', large, 'L')
assert large_a['pid'] != large_b['pid']

missing = directory('missing')
run('load', missing, code='not_found')
assert not record(missing).exists()
# Stale files are never promoted, regardless of whether committed data exists.
stale = missing / '.stale.part'
stale.write_bytes(record(store).read_bytes())
stale.chmod(0o600)
run('load', missing, code='not_found')
assert stale.exists() and not record(missing).exists()
stale2 = store / '.stale.part'
stale2.write_bytes(b'{corrupt')
stale2.chmod(0o600)
run('load', store, 'B')
assert stale2.read_bytes() == b'{corrupt'

# Syscall failures are armed solely in this test executable, around the public API.
for fault in ('write-error', 'file-fsync-error', 'rename-error', 'temp-close-error'):
    run('save', store, 'A')
    before = record(store).read_bytes()
    run('save', store, 'B', fault=fault, code='io_error')
    assert record(store).read_bytes() == before, fault
    run('load', store, 'A')
run('save', store, 'B', fault='dir-fsync-error', code='commit_uncertain')
run('load', store, 'B')
run('save', store, 'A', fault='short-eintr')
run('load', store, 'A')
run('save', store, 'B', fault='readback-error', code='commit_uncertain')
run('load', store, 'B')
run('save', store, 'A', fault='readback-close-error', code='commit_uncertain')
run('load', store, 'A', fault='read-short-eintr')
for fault in ('directory-open-error', 'record-open-error'):
    before = digest(record(store))
    run('load', store, 'A', fault=fault, code='io_error')
    assert digest(record(store)) == before
run('load', store, 'A')
run('save', store, 'B')
for fault in ('write-error', 'file-fsync-error', 'rename-error', 'temp-close-error'):
    fresh = directory('first-' + fault)
    run('save', fresh, 'B', fault=fault, code='io_error')
    assert not record(fresh).exists()
    run('load', fresh, 'B', code='not_found')
for fault in ('dir-fsync-error', 'readback-error', 'readback-close-error'):
    fresh = directory('first-' + fault)
    run('save', fresh, 'B', fault=fault, code='commit_uncertain')
    run('load', fresh, 'B')

# Stop the exact child only after its wrapper reports the actual commit boundary.
for first_publication in (False, True):
 for fault, phase, expected in (('before-rename', b'B', 'A'), ('after-rename', b'A', 'B')):
    destination = directory('first-kill-' + fault) if first_publication else store
    if not first_publication:
        run('save', destination, 'A')
        old_digest = digest(record(destination))
    read_fd, write_fd = os.pipe()
    env = dict(os.environ, PB_TEST_NOTIFY_FD=str(write_fd))
    child = subprocess.Popen(args('save', destination, 'B', fault=fault), stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, pass_fds=(write_fd,))
    os.close(write_fd)
    try:
        assert select.select([read_fd], [], [], 20)[0], 'writer never reached boundary'
        assert os.read(read_fd, 1) == phase
        child.send_signal(signal.SIGKILL)
        out, err = child.communicate(timeout=10)
        assert child.returncode == -signal.SIGKILL and not out, (out, err)
        receipts.append(dict(operation='interrupted_save', fault=fault, pid=child.pid, first_publication=first_publication, returncode=child.returncode, commit='unacknowledged'))
    finally:
        os.close(read_fd)
        if child.poll() is None:
            child.kill()
            child.communicate(timeout=10)
    if expected == 'A':
        if first_publication:
            assert not record(destination).exists()
            run('load', destination, 'B', code='not_found')
        else:
            assert digest(record(destination)) == old_digest
            run('load', destination, 'A')
    else:
        run('load', destination, 'B')

# Byte corruptions never rewrite the record or silently reset to an empty plan.
good = record(store).read_bytes()
bad_checksum = json.loads(good)
bad_checksum['checksum'] = '0' * 64
for name, blob in [('truncated', good[:-1]), ('trailing', good + b'{}'), ('duplicate', b'{"schema_version":"forged",' + good[1:]), ('garbage', b'not json'), ('checksum', json.dumps(bad_checksum).encode())]:
    record(store).write_bytes(blob)
    before = digest(record(store))
    run('load', store, 'B', code='invalid_record')
    run('save', store, 'B', code='invalid_record')
    assert digest(record(store)) == before
record(store).write_bytes(good)

# Recompute the non-authenticating checksum so semantic and binding checks,
# rather than mere accidental-corruption detection, must reject forged data.
def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode()

def forged_record(mutate, original=None):
    value = json.loads(good if original is None else original)
    value.pop('checksum')
    mutate(value)
    checksum_input = dict(digest_domain='cpi-211-offline-gear-plan-store-v1', payload=value)
    value['checksum'] = hashlib.sha256(canonical(checksum_input)).hexdigest()
    return canonical(value)

for pin in ('candidate_id', 'generation', 'graph_digest', 'manifest_digest', 'projection_digest'):
    def change(value, pin=pin):
        value['provenance'][pin] = 'candidate:other' if pin == 'candidate_id' else 'f' * 64
        if pin == 'graph_digest':
            value['plan']['content_graph_digest'] = 'f' * 64
    blob = forged_record(change)
    record(store).write_bytes(blob)
    run('load', store, 'B', code='invalid_context')
    assert record(store).read_bytes() == blob
for name, mutate, expected_code in (
    ('domain', lambda v: v.update(domain='effective_realm'), 'invalid_record'),
    ('blocked', lambda v: v['blocked_reasons'].update({'source:forged': 'forged_reason'}), 'invalid_context'),
    ('score', lambda v: v['plan']['evaluations'][0]['score'].update(total=123456), 'invalid_record'),
    ('enum', lambda v: v['plan']['evaluations'][0].update(disposition='forged'), 'invalid_record'),
    ('selection', lambda v: v['plan'].update(selected_item_id=None), 'invalid_record'),
    ('nested-graph', lambda v: v['plan'].update(content_graph_digest='f' * 64), 'invalid_record'),
):
    blob = forged_record(mutate)
    record(store).write_bytes(blob)
    run('load', store, 'B', code=expected_code)
    if expected_code == 'invalid_record':
        run('save', store, 'B', code='invalid_record')
    assert record(store).read_bytes() == blob, name
record(store).write_bytes(good)

# A fully checksummed selected source must belong to this target item in the
# independently loaded capsule, even when all domain crosslinks are coherent.
for kind in ('absent', 'wrong-item'):
    def change_source(value, kind=kind):
        plan = value['plan']
        selected = next(e for e in plan['evaluations'] if e['disposition'] == 'pursue')
        source = 'source:absent' if kind == 'absent' else next(e['source_id'] for e in plan['evaluations'] if e['item_id'] != selected['item_id'])
        selected['source_id'] = source
        plan['selected_source_id'] = source
        for e in plan['evaluations']:
            if e['eligible']:
                e['reason_codes'] = ['selected_highest_value' if e is selected else 'lower_ranked_than:' + source]
    blob = forged_record(change_source)
    record(store).write_bytes(blob)
    run('load', store, 'B', code='invalid_context')
    assert record(store).read_bytes() == blob
record(store).write_bytes(good)

blocked_store = directory('immutable-capsule-block')
run('save', blocked_store, 'C')
blocked_good = record(blocked_store).read_bytes()
def forge_blocked(value):
    plan = value['plan']
    blocked = next(e for e in plan['evaluations'] if e['source_id'] == 'source:00')
    assert 'source:00' in value['blocked_reasons'] and not blocked['eligible']
    for e in plan['evaluations']:
        if e['eligible']:
            e['disposition'] = 'deferred'
            e['reason_codes'] = ['lower_ranked_than:source:00']
    blocked.update(eligible=True, disposition='pursue', reason_codes=['selected_highest_value'])
    blocked['score'].update(upgrade_utility=100000, useful_life=0, profession=0, acquisition_time=0, money=0, risk=0, contention=0, total=100000, expected_attempts_numerator=1, expected_attempts_denominator=1, expected_attempts_ceiling=1, expected_acquisition_seconds=2)
    plan.update(selected_target_id=blocked['target_id'], selected_source_id='source:00', selected_item_id=blocked['item_id'])
blob = forged_record(forge_blocked, blocked_good)
record(blocked_store).write_bytes(blob)
run('load', blocked_store, 'C', code='invalid_context')
assert record(blocked_store).read_bytes() == blob

def below_policy_threshold(value):
    for e in value['plan']['evaluations']:
        if e['eligible']:
            e['score'].update(upgrade_utility=0, useful_life=0, profession=0,
                              acquisition_time=0, money=0, risk=0, contention=0, total=0)
blob = forged_record(below_policy_threshold)
record(store).write_bytes(blob)
run('load', store, 'B', code='invalid_context')
assert record(store).read_bytes() == blob
record(store).write_bytes(good)

# No blocked open and no external sentinel mutation, including write attempts.
sentinel = ROOT / 'external-sentinel'
sentinel.write_bytes(b'unchanged-external-target')
sentinel.chmod(0o600)
sentinel_digest = digest(sentinel)
for kind in ('symlink', 'hardlink', 'fifo', 'directory', 'broad-mode', 'oversize'):
    unsafe = directory('unsafe-' + kind)
    p = record(unsafe)
    if kind == 'symlink':
        p.symlink_to(sentinel)
    elif kind == 'hardlink':
        os.link(sentinel, p)
    elif kind == 'fifo':
        os.mkfifo(p, 0o600)
    elif kind == 'directory':
        p.mkdir(mode=0o700)
    elif kind == 'broad-mode':
        p.write_bytes(good)
        p.chmod(0o644)
    else:
        with p.open('wb') as stream:
            stream.truncate(17 * 1024 * 1024)
        p.chmod(0o600)
    expected_code = 'invalid_record' if kind == 'oversize' else 'unsafe_store'
    run('load', unsafe, 'B', code=expected_code)
    run('save', unsafe, 'B', code=expected_code)
    assert digest(sentinel) == sentinel_digest, kind

ancestor = ROOT / 'symlink-parent'
ancestor.symlink_to(store, target_is_directory=True)
run('load', ancestor, 'B', code='unsafe_store')
run('save', ancestor, 'B', code='unsafe_store')
broad = directory('broad-directory')
broad.chmod(0o755)
run('load', broad, code='unsafe_store')
broad.chmod(0o700)

# Keep small sanitized receipts, clean only exact files registered under this run.
report = ROOT / 'receipts.json'
report.write_text(json.dumps(dict(cases=len(receipts), receipts=receipts, fresh_process=True, host_power_loss_tested=False), indent=2) + '\n')
report.chmod(0o600)
for folder in ROOT.iterdir():
    if folder.is_dir() and not folder.is_symlink():
        for p in folder.iterdir():
            if p.is_dir() and not p.is_symlink():
                p.rmdir()
            else:
                p.unlink()
        folder.rmdir()
    elif folder != report:
        folder.unlink()
assert list(ROOT.iterdir()) == [report]
print(json.dumps(dict(status='passed', cases=len(receipts), receipt=str(report))))
