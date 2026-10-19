"""Exercise the real publisher against a stateful local GitHub CLI substitute."""
import json,os,subprocess,tempfile
from pathlib import Path
publisher=Path(__file__).with_name('publish_release.sh').resolve()
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory)
    gh=root/'gh'
    gh.write_text('''#!/usr/bin/env python3
import json,os,sys
from pathlib import Path
root=Path(os.environ['PUBLISH_TEST_ROOT']);args=sys.argv[1:];op=args[1]
with (root/'calls').open('a') as f:f.write(json.dumps(args)+'\\n')
exists=root/'exists';state=root/'failures';n=int(state.read_text()) if state.exists() else 0
mode=os.environ['PUBLISH_TEST_MODE']
if op=='view':sys.exit(0 if exists.exists() else 1)
if mode=='persistent' or (mode=='create' and op=='create' and n==0) or (mode=='upload' and op=='upload' and n==0):
 state.write_text(str(n+1));print('HTTP 403: Resource not accessible by integration',file=sys.stderr);sys.exit(1)
if op=='create':exists.touch()
sys.exit(0)
''')
    gh.chmod(0o755)
    sleep=root/'sleep';sleep.write_text('#!/bin/sh\nprintf "%s\\n" "$1" >> "$PUBLISH_TEST_ROOT/delays"\n');sleep.chmod(0o755)
    env=dict(os.environ,PATH=str(root)+os.pathsep+os.environ['PATH'],PUBLISH_TEST_ROOT=str(root),DUAL_TAG='dual-v1.1.0',GITHUB_SHA='test-commit')
    for mode,existing,tag in [('create',False,'mini-v1.1.0'),('upload',True,'dual-v1.1.0'),('persistent',False,'mini-v1.1.0')]:
        for name in ['calls','exists','failures','delays']:(root/name).unlink(missing_ok=True)
        if existing:(root/'exists').touch()
        result=subprocess.run(['sh',str(publisher),tag,'ButtonGoblin_Mini_v1_1_0','Button Goblin test','notes.md'],env=dict(env,PUBLISH_TEST_MODE=mode),capture_output=True,text=True)
        calls=[json.loads(line) for line in (root/'calls').read_text().splitlines()]
        if mode=='persistent':
            assert result.returncode!=0 and sum(c[1]=='create' for c in calls)==3
            assert (root/'delays').read_text().splitlines()==['5','10']
        elif mode=='create':
            assert result.returncode==0 and sum(c[1]=='create' for c in calls)==2
            assert '--latest=false' in calls[-1] and 'test-commit' in calls[-1]
        else:
            assert result.returncode==0 and [c[1] for c in calls]==['view','upload','view','upload','edit']
            assert '--clobber' in calls[-2] and '--latest=true' in calls[-1]
        if mode!='persistent':assert (root/'delays').read_text().splitlines()==['5']
print('PASS: create/upload failures retry safely; persistent failures stay failed; assets and latest metadata preserved')
