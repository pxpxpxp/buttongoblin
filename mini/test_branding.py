"""Audit new-install Button Goblin packages and the editor export namespace."""
from pathlib import Path
import re,subprocess,sys
mini,dual=map(Path,sys.argv[1:3])
for root,names in [(mini,{'buttongoblin-mini'}),(dual,{'buttongoblin','buttongoblin-mini'})]:
    assert {p.name for p in (root/'Scripts/.config').iterdir()}==names
    for app in (root/'Scripts/.config').iterdir():
        assert (app/'source/buttongoblin.c').exists()
        assert (app/'source/build.sh').exists()
        for p in app.rglob('*'):
            assert 'streamdeck' not in p.name.lower(),p
            if p.suffix in {'.c','.sh','.conf'}:
                text=p.read_text()
                assert 'streamdeck' not in text.lower(),p
                assert 'migrate_legacy' not in text,p
            if p.suffix=='.sh':subprocess.run(['sh','-n',str(p)],check=True)
    for p in (root/'Scripts').glob('*.sh'):
        assert p.name.startswith('ButtonGoblin_'),p
        subprocess.run(['sh','-n',str(p)],check=True)
editor=Path(__file__).resolve().parent.parent/'docs/editor/app.js'
text=editor.read_text()
assert "root = 'Scripts/.config/buttongoblin/'" in text
assert 'ButtonGoblin_Enable.sh' in text and 'streamdeck' not in text.lower()
assert "format: 'buttongoblin-v1'" in text
assert 'ButtonGoblin_Layout.zip' in text and 'ButtonGoblin_Project.json' in text
print('PASS: fresh Button Goblin folders, source, scripts, shell syntax and editor exports; no application aliases')
