"""Patch page-two hold feedback and verify Back acts on release."""
import sys
from pathlib import Path
app=Path(sys.argv[1])/'Scripts/.config/buttongoblin'
p=app/'source/buttongoblin.c';s=p.read_text()
old='if(cfg->keys[k].action==BACK_PAGE){if(states[k]&&!inputs.down[k]){inputs.down[k]=1;switch_page(0);return;}inputs.down[k]=states[k];continue;}'
new='if(cfg->keys[k].action==BACK_PAGE){if(states[k]!=inputs.down[k]){visual(k,states[k]?1:0,now);restore[k]=0;if(!states[k]){inputs.down[k]=0;switch_page(0);return;}}inputs.down[k]=states[k];continue;}'
assert old in s;s=s.replace(old,new)
old='visual(k,rc?2:(states[k]?1:0),now);'
assert old in s;s=s.replace(old,old+'if(!rc)restore[k]=0;')
p.write_text(s)
p=app/'source/test_pages.c';s=p.read_text()
old='state[14]=1;handle_input(state,3.2,capture);assert(active_page==0&&!inputs.armed[14]&&uploads[13].wanted==3);'
new='state[14]=1;handle_input(state,3.2,capture);assert(active_page==1&&uploads[14].wanted==1&&restore[14]==0);state[14]=0;handle_input(state,3.25,capture);assert(active_page==0&&uploads[13].wanted==3);'
assert old in s;s=s.replace(old,new)
old='  keyboard_close();close(pipes[0]);'
new='''  switch_page(1);page_configs[1]->keys[0].action=UP_BUTTON;
  inputs.armed[0]=1;inputs.down[0]=0;state[0]=1;
  handle_input(state,6,capture);assert(uploads[0].wanted==1&&restore[0]==0);
  state[0]=0;handle_input(state,9,capture);assert(uploads[0].wanted==0);
  keyboard_close();close(pipes[0]);'''
assert old in s;s=s.replace(old,new);p.write_text(s)
print('PASS: held feedback persists until release; Back returns after green held feedback')
