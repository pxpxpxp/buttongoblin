"""Release names shared by package preparation and GitHub Actions."""
from pathlib import Path
import argparse

MINI_VERSION = '1.1.0'
DUAL_VERSION = '1.1.0'
REPOSITORY = 'https://github.com/pxpxpxp/buttongoblin'

def bundle(device, version):
    return f'ButtonGoblin_{device}_v{version.replace(".", "_")}'

MINI_BUNDLE = bundle('Mini', MINI_VERSION)
DUAL_BUNDLE = bundle('Dual', DUAL_VERSION)
MINI_TAG = f'mini-v{MINI_VERSION}'
DUAL_TAG = f'dual-v{DUAL_VERSION}'
MINI_DOWNLOAD = f'{REPOSITORY}/releases/download/{MINI_TAG}/{MINI_BUNDLE}.zip'
DUAL_DOWNLOAD = f'{REPOSITORY}/releases/download/{DUAL_TAG}/{DUAL_BUNDLE}.zip'

if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--github-env',type=Path,required=True)
    args=parser.parse_args()
    native=Path(__file__).with_name('buttongoblin-mini.c').read_text()
    if f'#define VERSION "Button Goblin Mini {MINI_VERSION}"' not in native:
        raise SystemExit('Mini source version must match releases.py')
    mk2=(Path(__file__).resolve().parent.parent/'mk2/buttongoblin-mk2.c').read_text()
    if f'#define VERSION "Button Goblin MK.2 {DUAL_VERSION}"' not in mk2:
        raise SystemExit('MK.2 source version must match releases.py')
    names=['MINI_VERSION','DUAL_VERSION','MINI_BUNDLE','DUAL_BUNDLE','MINI_TAG','DUAL_TAG']
    with args.github_env.open('a') as output:
        for name in names:output.write(f'{name}={globals()[name]}\n')
