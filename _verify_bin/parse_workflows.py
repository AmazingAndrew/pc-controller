import yaml
import sys
from pathlib import Path

for f in sorted(Path('.github/workflows').glob('*.y*ml')):
    text = f.read_text(encoding='utf-8')
    try:
        yaml.safe_load(text)
        print(f'{f.name}: PASS')
    except yaml.YAMLError as e:
        print(f'{f.name}: FAIL')
        print(f'  Error: {str(e)[:200]}')
