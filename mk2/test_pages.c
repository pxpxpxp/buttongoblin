#define main app_main
#include "buttongoblin-mk2.c"
#undef main
#include <assert.h>
static int events;
static void capture(int k,double now){assert(k==14);(void)now;events++;}
int main(void){
  page_configs[0]=newconfig();page_configs[1]=newconfig();cfg=page_configs[0];
  for(int k=0;k<15;k++)page_configs[1]->keys[k].action=NONE;
  page_configs[1]->keys[14].action=BACK_PAGE;
  active_page=0;reset_usb_state();uint8_t state[15]={0};
  state[14]=1;handle_input(state,1,capture);assert(!events);
  state[14]=0;handle_input(state,1.2,capture);assert(events==1&&active_page==0);
  state[14]=1;handle_input(state,2,capture);handle_holds(2.99,capture);assert(active_page==0);
  enqueue(&queue,1);uploads[13].wanted=3;
  handle_holds(3.01,capture);assert(active_page==1&&queue.count==0&&!inputs.armed[14]);
  state[14]=0;handle_input(state,3.1,capture);assert(active_page==1&&events==1);
  state[14]=1;handle_input(state,3.2,capture);assert(active_page==1&&uploads[14].wanted==1&&restore[14]==0);state[14]=0;handle_input(state,3.25,capture);assert(active_page==0&&uploads[13].wanted==3);
  state[14]=0;handle_input(state,3.3,capture);assert(events==1);
  page_configs[0]->keys[14].trigger=HOLD;
  state[14]=1;handle_input(state,3.4,capture);handle_holds(4.1,capture);
  assert(active_page==0&&events==1);
  handle_holds(4.41,capture);assert(active_page==1&&events==1);
  state[14]=0;handle_input(state,4.5,capture);switch_page(0);
  page_configs[0]->keys[14].trigger=PRESS;
  /* A release report past the threshold also counts as a hold. */
  state[14]=1;handle_input(state,4,capture);state[14]=0;handle_input(state,5.1,capture);assert(active_page==1&&events==1);
  /* Updater completion belongs to the hidden console page. */
  page_uploads[0][13].wanted=1;
  pid_t child=fork();assert(child>=0);if(!child)_exit(0);
  script_pid=child;script_key=13;script_page=0;script_action=UPDATE_ALL;
  strcpy(script_target,page_configs[0]->keys[13].target);script_started=nowtime();
  for(int i=0;i<100&&script_pid>0;i++){reap_page_script(nowtime(),0);usleep(1000);}
  assert(script_pid<0&&page_uploads[0][13].wanted==3&&uploads[13].wanted==0);
  switch_page(0);assert(uploads[13].wanted==3);
  int pipes[2];assert(pipe(pipes)==0);keyboard_fd=pipes[1];
  const int expected[]={KEY_SPACE,KEY_F12,KEY_ENTER,KEY_UP,KEY_DOWN,KEY_LEFT,KEY_RIGHT,KEY_ESC};
  for(int i=0;i<8;i++){struct input_event ev[2];assert(control_change(SPACE_BUTTON+i,1)==0);assert(read(pipes[0],ev,sizeof ev)==sizeof ev);assert(ev[0].code==expected[i]&&ev[0].value==1&&ev[1].type==EV_SYN);assert(control_change(SPACE_BUTTON+i,0)==0);assert(read(pipes[0],ev,sizeof ev)==sizeof ev);assert(ev[0].value==0);}
  switch_page(1);page_configs[1]->keys[0].action=UP_BUTTON;
  inputs.armed[0]=1;inputs.down[0]=0;state[0]=1;
  handle_input(state,6,capture);assert(uploads[0].wanted==1&&restore[0]==0);
  state[0]=0;handle_input(state,9,capture);assert(uploads[0].wanted==0);
  keyboard_close();close(pipes[0]);
  /* Custom keyboard HOLD settings must not schedule an unhandled deadline. */
  memset(&queue,0,sizeof queue);memset(&inputs,0,sizeof inputs);page_transition=0;
  for(int k=0;k<KEYS;k++){uploads[k].current=uploads[k].wanted=0;uploads[k].finished=1;uploads[k].frame_due=restore[k]=0;}
  cfg->keys[0].trigger=HOLD;inputs.armed[0]=inputs.down[0]=1;
  assert(worker_wait_ms(10,1,20,20,20,20,0)==1000);
  uploads[0].wanted=1;switch_page(0);switch_page(1);assert(uploads[0].wanted==0);
  /* A custom script on page two updates that page without swapping cfg. */
  page_configs[1]->keys[13].action=UPDATE_ALL;
  strcpy(page_configs[1]->keys[13].target,page_configs[0]->keys[13].target);
  switch_page(0);uploads[13].wanted=0;page_uploads[1][13].wanted=1;
  child=fork();assert(child>=0);if(!child)_exit(0);
  script_pid=child;script_key=13;script_page=1;script_action=UPDATE_ALL;
  strcpy(script_target,page_configs[1]->keys[13].target);script_started=nowtime();
  for(int i=0;i<200&&script_pid>0;i++){reap_page_script(nowtime(),0);usleep(1000);}
  assert(script_pid<0&&page_uploads[1][13].wanted==3&&uploads[13].wanted==0&&active_page==0&&cfg==page_configs[0]);
  /* Slow initial page painting must complete before animations can restart. */
  switch_page(1);
  for(int k=0;k<KEYS;k++){
    struct image *im=&cfg->keys[k].images[uploads[k].wanted];
    im->frames=calloc(2,sizeof(struct frame));assert(im->frames);im->frame_count=2;
    uploads[k].current=uploads[k].wanted;uploads[k].finished=1;uploads[k].frame_due=10+k*.1+.2;
    advance_animations(10+k*.1);
  }
  assert(!upload_pending());
  freeconfig(page_configs[0]);freeconfig(page_configs[1]);cfg=NULL;page_configs[0]=page_configs[1]=NULL;
  puts("PASS: brightness tap, timed hold, late release, held-key suppression, Back, queue cancellation, retained hidden updater completion");
}
