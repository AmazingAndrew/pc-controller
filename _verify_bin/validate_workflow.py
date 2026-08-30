import yaml
import re
import sys
from pathlib import Path

WORKFLOW = Path('.github/workflows/build-presenter.yml')
FULL_SHA_RE = re.compile(r'^[0-9a-f]{40}$')

text = WORKFLOW.read_text(encoding='utf-8')
data = yaml.safe_load(text)
print('YAML parse: PASS')
print('name:', data.get('name'))
print('on keys:', sorted(data.get(True, data.get('on', {})).keys()))
print('permissions:', data.get('permissions'))
print('concurrency:', data.get('concurrency'))
print('jobs:')
for job_name, job in data.get('jobs', {}).items():
    needs = job.get('needs', '-')
    runs_on = job.get('runs-on', '-')
    steps = job.get('steps', [])
    print(f'  - {job_name}: runs_on={runs_on}, needs={needs}, steps={len(steps)}')

print()
print('Action pins check:')
errors = 0
for i, line in enumerate(text.splitlines(), 1):
    m = re.match(r"\s*-?\s*uses:\s*['\"]?([^'\"\s]+)", line)
    if not m:
        continue
    action = m.group(1)
    if action.startswith('./') or action.startswith('docker://'):
        continue
    if '@' not in action:
        print(f'  L{i}: NO VERSION PIN: {action}')
        errors += 1
        continue
    version = action.rsplit('@', 1)[1]
    status = 'OK' if FULL_SHA_RE.fullmatch(version) else 'FAIL'
    print(f'  L{i}: [{status}] {action}')
    if status != 'OK':
        errors += 1

print()
print(f'Action pin errors: {errors}')
sys.exit(0 if errors == 0 else 1)
