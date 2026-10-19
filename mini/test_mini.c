#define main mini_program_main
#include "buttongoblin-mini.c"
#undef main
#include <assert.h>
#include <sys/socket.h>


/* Duplicate mappings share one virtual key until the last armed button releases. */
static void duplicate_shortcut_tests(void) {
  cfg=newconfig();assert(cfg);active_page=0;reset_usb_state();
  cfg->keys[0].action=cfg->keys[1].action=UP_BUTTON;
  int fds[2];assert(!pipe(fds));keyboard_fd=fds[1];
  assert(fcntl(fds[0],F_SETFL,O_NONBLOCK)==0);
  struct input_event ev[2];uint8_t state[KEYS]={0};
  state[0]=1;handle_input(state,1,NULL);
  assert(read(fds[0],ev,sizeof ev)==sizeof ev&&ev[0].value==1);
  state[1]=1;handle_input(state,2,NULL);assert(read(fds[0],ev,sizeof ev)<0&&errno==EAGAIN);
  state[0]=0;handle_input(state,3,NULL);
  assert(read(fds[0],ev,sizeof ev)<0&&errno==EAGAIN);
  assert(control_down[UP_BUTTON-SPACE_BUTTON]&&uploads[0].wanted==0&&uploads[1].wanted==1);
  state[0]=1;state[1]=0;handle_input(state,4,NULL);
  assert(read(fds[0],ev,sizeof ev)<0&&errno==EAGAIN);
  state[0]=0;handle_input(state,5,NULL);
  assert(read(fds[0],ev,sizeof ev)==sizeof ev&&ev[0].value==0);
  /* A button suppressed after reconnect must not hold the virtual key down. */
  state[1]=1;inputs.down[1]=1;inputs.armed[1]=0;
  state[0]=1;handle_input(state,6,NULL);
  assert(read(fds[0],ev,sizeof ev)==sizeof ev&&ev[0].value==1);
  state[0]=0;handle_input(state,7,NULL);
  assert(read(fds[0],ev,sizeof ev)==sizeof ev&&ev[0].value==0);
  keyboard_close();close(fds[0]);freeconfig(cfg);cfg=NULL;
  puts("PASS: duplicate shortcut holds, same-report handoff and reconnect suppression");
}

static int fired;
static void capture(int key,double now){(void)now;assert(key==4);fired++;}
static void arm_inputs(void){memset(&inputs,0,sizeof inputs);memset(inputs.armed,1,sizeof inputs.armed);}
static int device_events,last_device_key;
static void device_capture(int key,double now){(void)now;device_events++;last_device_key=key;}

static void input_reader_tests(void){
  /* Packet boundaries model HID reads: unread padding is discarded, unlike a pipe. */
  int sockets[2];assert(!socketpair(AF_UNIX,SOCK_SEQPACKET,0,sockets));
  uint8_t report[65]={1,1,0,1,0,1,0},states[KEYS];
  const size_t lengths[]={7,8,17,64,65};
  for(unsigned i=0;i<sizeof lengths/sizeof lengths[0];i++){
    assert(write(sockets[0],report,lengths[i])==(ssize_t)lengths[i]);
    assert(read_key_report(sockets[1],states)==1);
    assert(!memcmp(states,report+1,KEYS));
  }
  assert(write(sockets[0],report,6)==6&&read_key_report(sockets[1],states)==0);
  report[1]=2;assert(write(sockets[0],report,65)==65&&read_key_report(sockets[1],states)==0);
  report[1]=1;assert(write(sockets[0],report,7)==7&&read_key_report(sockets[1],states)==1);
  close(sockets[0]);close(sockets[1]);
  puts("PASS: actual seven-byte reads accept padded input packets and reject malformed prefixes");
}

static void transition_tests(void){
  for(int p=0;p<PAGES;p++){
    page_configs[p]=newconfig();assert(page_configs[p]);
    for(int k=0;k<KEYS;k++){
      page_configs[p]->keys[k].action=LAUNCH;
      page_configs[p]->keys[k].images[0].frames=calloc(2,sizeof(struct frame));
      assert(page_configs[p]->keys[k].images[0].frames);
      page_configs[p]->keys[k].images[0].frame_count=2;
    }
  }
  cfg=page_configs[0];active_page=0;startup_stage=0;completed_key=completed_page=-1;
  reset_usb_state();switch_page(1);
  /* Six slow paints take longer than the first key's animation delay. */
  for(int k=0;k<KEYS;k++){
    uploads[k].current=0;uploads[k].finished=1;uploads[k].frame_due=10+k*.1+.35;
    advance_animations(10+k*.1);
    if(!k){int wait=worker_wait_ms(50,1,52,51,90,50.5,0);assert(wait>=499&&wait<=501);}
  }
  assert(!upload_pending());
  uint8_t states[KEYS]={1,0,0,0,0,0};
  handle_device_input(states,10.6,device_capture);
  assert(!page_transition&&device_events==1&&last_device_key==0);
  states[0]=0;handle_device_input(states,10.7,device_capture);
  switch_page(2);states[0]=1;handle_device_input(states,11,device_capture);
  assert(device_events==1&&!inputs.armed[0]);
  for(int k=0;k<KEYS;k++){uploads[k].current=uploads[k].wanted;uploads[k].finished=1;}
  handle_device_input(states,12,device_capture);assert(device_events==1&&!page_transition);
  states[0]=0;handle_device_input(states,12.1,device_capture);
  states[0]=1;handle_device_input(states,12.2,device_capture);assert(device_events==2);
  /* A key held through the startup fade must remain disarmed afterward. */
  states[0]=0;handle_device_input(states,13,device_capture);
  startup_stage=3;startup_since=12;startup_level=0;
  states[4]=1;handle_device_input(states,13.1,device_capture);
  assert(!startup_step(-1,65,14)&&startup_stage==4&&inputs.down[4]&&!inputs.armed[4]);
  startup_stage=0;states[0]=1;handle_device_input(states,13.2,device_capture);
  assert(device_events==3&&last_device_key==0&&!inputs.armed[4]);
  for(int p=0;p<PAGES;p++){freeconfig(page_configs[p]);page_configs[p]=NULL;}
  cfg=NULL;active_page=page_transition=0;startup_level=-1;memset(&queue,0,sizeof queue);
  puts("PASS: slow animated page paint unblocks presses, held-key suppression and startup input tracking");
}

static void protocol_tests(void){
  assert(!selftest());
  uint8_t report[1024],payload[1008],states[6];
  memset(payload,0x5a,sizeof payload);
  for(int key=0;key<6;key++)for(unsigned chunk=0;chunk<20;chunk++){
    packet(report,key,chunk,payload,chunk==19?102:1008,chunk==19);
    assert(report[0]==2&&report[1]==1&&report[2]==chunk&&report[3]==0);
    assert(report[4]==(chunk==19)&&report[5]==key+1);
    for(int i=6;i<16;i++)assert(!report[i]);
    assert(!memcmp(report+16,payload,chunk==19?102:1008));
    if(chunk==19)for(int i=118;i<1024;i++)assert(report[i]==0);
  }
  uint8_t raw[65]={1,1,0,1,0,1,0};
  assert(!decode(raw,7,states)&&!decode(raw,65,states));
  for(int n=0;n<65;n++)if(n!=7)assert(decode(raw,n,states)==-1);
  raw[0]=2;assert(decode(raw,7,states)==-1);raw[0]=1;
  for(int k=1;k<=6;k++){raw[k]=2;assert(decode(raw,65,states)==-1);raw[k]=0;}
  uint8_t f[17];brightness_report(f,-1);assert(f[5]==0);
  brightness_report(f,999);assert(f[5]==100);
  for(int i=6;i<17;i++)assert(f[i]==0);
  struct rawinfo device={BUS_USB,0x0fd9,0x0063};assert(mini_identity(&device));
  for(unsigned pid=0;pid<=0xffff;pid++){device.product=(short)pid;assert(mini_identity(&device)==mini_product(pid));}
  device.product=0x63;device.vendor=0x045e;assert(!mini_identity(&device));
  device.vendor=0x0fd9;device.bustype=BUS_BLUETOOTH;assert(!mini_identity(&device));
  puts("PASS: all Mini keys, all 20 BMP chunks, report padding, malformed input and brightness bounds");
}

static void page_tests(void){
  for(int p=0;p<PAGES;p++){page_configs[p]=newconfig();assert(page_configs[p]);}
  cfg=page_configs[0];active_page=0;reset_usb_state();
  for(int p=0;p<PAGES;p++)page_configs[p]->keys[5].action=NEXT_PAGE;
  page_configs[4]->keys[3].action=BACK_PAGE;
  page_configs[0]->keys[4].action=CYCLE;
  uint8_t state[KEYS]={0};
  for(int page=0;page<PAGES;page++){
    arm_inputs();state[5]=1;handle_input(state,10+page,capture);
    assert(active_page==page&&uploads[5].wanted==1&&restore[5]==0);
    enqueue(&queue,0);state[5]=0;handle_input(state,10.1+page,capture);
    assert(active_page==(page+1)%PAGES&&queue.count==0);
  }
  arm_inputs();state[4]=1;handle_input(state,20,capture);
  state[4]=0;handle_input(state,20.1,capture);assert(fired==1&&active_page==0);
  state[4]=1;handle_input(state,21,capture);handle_holds(22.01,capture);
  assert(active_page==NAV_PAGE&&!inputs.armed[4]);
  state[4]=0;handle_input(state,22.1,capture);assert(fired==1);
  /* The Home navigation hold overrides a custom generic hold trigger. */
  switch_page(0);arm_inputs();cfg->keys[4].trigger=HOLD;
  state[4]=1;handle_input(state,26,capture);handle_holds(26.7,capture);
  assert(active_page==0&&fired==1);
  handle_holds(27.01,capture);assert(active_page==NAV_PAGE&&fired==1);
  state[4]=0;handle_input(state,27.1,capture);
  page_configs[0]->keys[4].trigger=PRESS;
  switch_page(4);arm_inputs();state[3]=1;handle_input(state,23,capture);
  assert(active_page==4&&uploads[3].wanted==1&&restore[3]==0);
  state[3]=0;handle_input(state,23.1,capture);assert(active_page==0);
  /* Held keys during a page change must not produce a new press. */
  arm_inputs();state[0]=1;inputs.down[0]=1;switch_page(3);
  assert(!inputs.armed[0]);handle_input(state,24,capture);assert(!inputs.armed[0]);
  state[0]=0;handle_input(state,24.1,capture);assert(inputs.armed[0]);
  int pipes[2];assert(!pipe(pipes));keyboard_fd=pipes[1];
  cfg->keys[0].action=UP_BUTTON;arm_inputs();state[0]=1;handle_input(state,25,capture);
  assert(uploads[0].wanted==1&&restore[0]==0);
  struct input_event event[2];assert(read(pipes[0],event,sizeof event)==sizeof event);
  assert(event[0].code==KEY_UP&&event[0].value==1);
  state[0]=0;handle_input(state,30,capture);assert(uploads[0].wanted==0);
  assert(read(pipes[0],event,sizeof event)==sizeof event);assert(!event[0].value);
  keyboard_close();close(pipes[0]);
  /* Update All completion must be delivered to Home/key 4 while hidden. */
  page_configs[0]->keys[3].action=UPDATE_ALL;strcpy(page_configs[0]->keys[3].target,"Scripts/update_all.sh");
  active_page=4;cfg=page_configs[4];reset_usb_state();
  pid_t child=fork();assert(child>=0);if(!child)_exit(0);
  script_pid=child;script_key=3;script_page=0;script_action=UPDATE_ALL;strcpy(script_target,"Scripts/update_all.sh");script_started=nowtime();
  page_uploads[0][3].wanted=1;
  for(int i=0;i<200&&script_pid>0;i++){reap_page_script(nowtime(),0);usleep(1000);}
  assert(script_pid<0&&page_uploads[0][3].wanted==3&&uploads[3].wanted==0);
  switch_page(0);assert(uploads[3].wanted==3);
  /* A script mapped elsewhere must not update the same key on Home. */
  page_configs[2]->keys[3].action=UPDATE_ALL;strcpy(page_configs[2]->keys[3].target,"Scripts/update_all.sh");
  child=fork();assert(child>=0);if(!child)_exit(1);
  script_pid=child;script_key=3;script_page=2;script_action=UPDATE_ALL;strcpy(script_target,"Scripts/update_all.sh");script_started=nowtime();
  page_uploads[2][3].wanted=1;
  for(int i=0;i<200&&script_pid>0;i++){reap_page_script(nowtime(),0);usleep(1000);}
  assert(script_pid<0&&page_uploads[2][3].wanted==2&&uploads[3].wanted==3);
  switch_page(2);assert(uploads[3].wanted==2);reset_usb_state();assert(uploads[3].wanted==2);
  switch_page(0);assert(uploads[3].wanted==3);
  /* Back is processed before key 5: capture the full report on switching. */
  switch_page(4);arm_inputs();memset(state,0,sizeof state);state[3]=1;handle_input(state,31,capture);
  state[3]=0;state[4]=1;handle_input(state,31.1,capture);
  assert(active_page==0&&inputs.down[4]&&!inputs.armed[4]);
  handle_input(state,31.2,capture);assert(!inputs.armed[4]);
  state[4]=0;handle_input(state,31.3,capture);assert(inputs.armed[4]);
  /* Leaving while a shortcut is held must not retain its green image. */
  switch_page(3);uploads[0].wanted=1;restore[0]=0;switch_page(0);switch_page(3);assert(uploads[0].wanted==0);
  /* Custom hold settings and transition-suppressed holds must not busy-spin. */
  switch_page(0);arm_inputs();memset(&queue,0,sizeof queue);
  for(int k=0;k<KEYS;k++){uploads[k].current=uploads[k].wanted=0;uploads[k].finished=1;uploads[k].frame_due=restore[k]=0;}
  cfg->keys[4].action=LAUNCH;cfg->keys[4].trigger=PRESS;inputs.down[4]=1;inputs.since[4]=1;page_transition=0;
  assert(worker_wait_ms(10,1,20,20,20,20,0)==1000);
  cfg->keys[4].action=UP_BUTTON;cfg->keys[4].trigger=HOLD;
  assert(worker_wait_ms(10,1,20,20,20,20,0)==1000);
  cfg->keys[4].action=LAUNCH;page_transition=1;
  assert(worker_wait_ms(10,1,20,20,20,20,0)==1000);
  page_transition=0;assert(worker_wait_ms(10,1,20,20,20,20,0)==0);
  inputs.held[4]=1;assert(worker_wait_ms(10,1,20,20,20,20,0)==1000);
  assert(next_brightness(CYCLE,25)==50&&next_brightness(CYCLE,100)==10);
  for(int p=0;p<PAGES;p++){freeconfig(page_configs[p]);page_configs[p]=NULL;}cfg=NULL;
  puts("PASS: six-page wrap, simultaneous holds, feedback reset, script ownership/reconnect and idle scheduling");
}

static void confirmation_tests(void){
  for(int p=0;p<PAGES;p++){page_configs[p]=newconfig();assert(page_configs[p]);}
  active_page=5;cfg=page_configs[5];
  cfg->keys[0].action=Y_BUTTON;cfg->keys[1].action=N_BUTTON;
  reset_usb_state();arm_inputs();
  int pipes[2];assert(!pipe(pipes));keyboard_fd=pipes[1];
  struct input_event events[4];uint8_t states[KEYS]={1,0,0,0,0,0};
  handle_input(states,1,device_capture);
  assert(read(pipes[0],events,2*sizeof *events)==2*sizeof *events);
  assert(events[0].type==EV_KEY&&events[0].code==KEY_Y&&events[0].value==1);
  assert(events[1].type==EV_SYN&&uploads[0].wanted==1&&restore[0]==0);
  handle_input(states,2,device_capture);
  struct pollfd pending={pipes[0],POLLIN,0};assert(poll(&pending,1,0)==0);
  states[0]=0;handle_input(states,3,device_capture);
  assert(read(pipes[0],events,2*sizeof *events)==2*sizeof *events);
  assert(events[0].code==KEY_Y&&events[0].value==0&&uploads[0].wanted==0);
  states[0]=states[1]=1;handle_input(states,4,device_capture);
  assert(read(pipes[0],events,sizeof events)==sizeof events);
  assert(events[0].code==KEY_Y&&events[0].value==1);
  assert(events[2].code==KEY_N&&events[2].value==1&&uploads[1].wanted==1);
  switch_page(0);
  assert(read(pipes[0],events,sizeof events)==sizeof events);
  assert(events[0].code==KEY_Y&&events[0].value==0);
  assert(events[2].code==KEY_N&&events[2].value==0);
  assert(!inputs.armed[0]&&!inputs.armed[1]);
  assert(!page_uploads[5][0].wanted&&!page_uploads[5][1].wanted);
  keyboard_close();close(pipes[0]);
  for(int p=0;p<PAGES;p++){freeconfig(page_configs[p]);page_configs[p]=NULL;}
  cfg=NULL;active_page=page_transition=0;
  puts("PASS: Y/N key events, held feedback, duplicate suppression and release on page change");
}

static void package_tests(const char *root){
  assert(!textset(configpath,sizeof configpath,root));
  char *slash=strrchr(configpath,'/');assert(slash);
  assert(!textset(assets,sizeof assets,root));*strrchr(assets,'/')=0;
  assert(strlen(assets)+6<sizeof assets);strcat(assets,"/icons");
  struct config *pages[PAGES]={readconfig(1)};assert(pages[0]);assert(!load_pages(pages));
  assert(pages[0]->keys[3].action==UPDATE_ALL&&!strcmp(pages[0]->keys[3].target,"Scripts/update_all.sh"));
  assert(pages[5]->keys[0].action==Y_BUTTON&&pages[5]->keys[1].action==N_BUTTON);
  assert(pages[5]->keys[3].action==BACK_PAGE);
  for(int p=0;p<PAGES;p++){
    assert(pages[p]->keys[5].action==NEXT_PAGE);
    for(int k=0;k<KEYS;k++)for(int state=0;state<3;state++){
      struct image *im=&pages[p]->keys[k].images[state];assert(im->frame_count>0);
      for(unsigned f=0;f<im->frame_count;f++)assert(validbmp(im->frames[f].data,im->frames[f].size));
    }
  }
  /* Serialize one whole frame and verify all chunks concatenate exactly. */
  cfg=pages[0];page_configs[0]=cfg;reset_usb_state();
  int feedback_pipe[2];assert(!pipe(feedback_pipe));uint8_t feedback[1024];
  for(int k=0;k<KEYS;k++){uploads[k].current=0;uploads[k].finished=0;}
  uploads[5].wanted=1;
  assert(upload_step(feedback_pipe[1])==1);assert(read(feedback_pipe[0],feedback,sizeof feedback)==1024);
  assert(feedback[5]==6); /* Changed state wins over ordinary animation work. */
  uploads[4].wanted=1;assert(upload_step(feedback_pipe[1])==1);
  assert(read(feedback_pipe[0],feedback,sizeof feedback)==1024&&feedback[5]==6&&feedback[2]==1);
  close(feedback_pipe[0]);close(feedback_pipe[1]);reset_usb_state();
  for(int k=0;k<KEYS;k++){uploads[k].current=0;uploads[k].finished=1;}
  uploads[0].finished=0;
  int pair[2];assert(!pipe(pair));uint8_t report[1024],assembled[19254];size_t used=0;
  for(int n=0;n<20;n++){
    assert(upload_step(pair[1])==1);assert(read(pair[0],report,sizeof report)==1024);
    size_t count=sizeof assembled-used;if(count>1008)count=1008;
    assert(report[2]==n&&report[5]==1&&report[4]==(n==19));memcpy(assembled+used,report+16,count);used+=count;
  }
  assert(used==sizeof assembled&&!memcmp(assembled,cfg->keys[0].images[0].frames[0].data,used));
  close(pair[0]);close(pair[1]);cfg=NULL;page_configs[0]=NULL;
  for(int p=0;p<PAGES;p++)freeconfig(pages[p]);
  puts("PASS: real artwork, Home updater, priority feedback without interleaving, cache bounds and full image serialization");
}

static void script_tests(void){
  char directory[]="/tmp/mini-script-test.XXXXXX";assert(mkdtemp(directory));
  char path[PATHCAP];assert(!join(path,sizeof path,directory,"job.sh"));
  strcpy(base,directory);assert(!join(runtime,sizeof runtime,directory,"service"));
  page_configs[0]=cfg=newconfig();assert(cfg);active_page=0;
  cfg->keys[0].action=UPDATE_ALL;strcpy(cfg->keys[0].target,path);
  for(int mode=0;mode<3;mode++){
    FILE *f=fopen(path,"w");assert(f);
    fputs(mode==0?"#!/bin/sh\nexec >/dev/null 2>&1\nsleep 0.02\nexit 0\n":mode==1?"#!/bin/sh\nexit 7\n":"#!/bin/sh\nsleep 30\n",f);assert(!fclose(f));
    assert(!start_script(0,-1,-1));assert(script_page==0&&script_timeout_seconds==3600);
    if(mode==2)script_started-=3601;
    for(int i=0;i<2000&&script_pid>0;i++){reap_page_script(nowtime(),0);usleep(1000);}
    assert(script_pid<0&&uploads[0].wanted==(mode==0?3:2));
    reset_usb_state();assert(uploads[0].wanted==(mode==0?3:2));
  }
  freeconfig(cfg);cfg=page_configs[0]=NULL;completed_key=completed_page=-1;
  unlink(path);assert(!runtimepath(path,".log"));unlink(path);
  assert(!runtimepath(path,".script.log"));unlink(path);assert(!rmdir(directory));
  strcpy(base,"/media/fat");strcpy(runtime,"/tmp/buttongoblin_mini");
  puts("PASS: real script guardian, closed output, success/failure, timeout and reconnect feedback");
}

int main(int argc,char **argv){duplicate_shortcut_tests();protocol_tests();input_reader_tests();transition_tests();page_tests();confirmation_tests();script_tests();if(argc>1)package_tests(argv[1]);return 0;}
