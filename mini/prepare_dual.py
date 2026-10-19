"""Bundle isolated MK.2 and Mini services without mixing configurations."""
import argparse, shutil
from pathlib import Path
from releases import DUAL_VERSION

def prepare(base,mini,output):
    shutil.copytree(base/'Scripts',output/'Scripts',dirs_exist_ok=True)
    shutil.copytree(mini/'Scripts',output/'Scripts',dirs_exist_ok=True)
    app=output/'Scripts/.config/buttongoblin'
    native=Path(__file__).resolve().parent.parent/'mk2'
    shutil.copy2(native/'buttongoblin-mk2.c',app/'source/buttongoblin.c')
    for name in ['test_pages.c','test_internal.c']:
        test=(native/name).read_text().replace('#include "buttongoblin-mk2.c"','#include "buttongoblin.c"')
        (app/'source'/name).write_text(test)
    for name in ['LICENSE.txt','ARTWORK_CREDITS.txt']:
        shutil.copy2(base/name,output/name)
    for name in ['Mini_Guide.html','MINI_LAYOUT.json']:
        if (mini/name).exists():shutil.copy2(mini/name,output/name)
    docs=Path(__file__).resolve().parent.parent/'docs'
    shutil.copy2(docs/'index.html',output/'Guide.html')
    shutil.copytree(docs/'editor',output/'Deck_Editor',dirs_exist_ok=True)
    shutil.copytree(docs/'assets',output/'assets',dirs_exist_ok=True)
    (output/'README.txt').write_text(Path(__file__).with_name('DUAL_README.md').read_text())
    for operation in ['Enable','Disable']:
        # Keep boot edits serial; a failure must remain visible to the user.
        script=f'''#!/bin/sh
set -u
status=0
sh /media/fat/Scripts/ButtonGoblin_{operation}.sh || status=1
sh /media/fat/Scripts/ButtonGoblin_Mini_{operation}.sh || status=1
if [ "$status" -eq 0 ]; then
  echo 'Both Button Goblin services {operation.lower()}d.'
else
  echo 'One or more Button Goblin operations failed; see the output above.' >&2
fi
exit "$status"
'''
        (output/'Scripts'/f'ButtonGoblin_Both_{operation}.sh').write_text(script)
    for p in (output/'Scripts').rglob('*.sh'):p.chmod(0o755)
    print(f'Prepared Dual v{DUAL_VERSION}: isolated services and shared Update All lock')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('base',type=Path);p.add_argument('mini',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();prepare(a.base,a.mini,a.output)
