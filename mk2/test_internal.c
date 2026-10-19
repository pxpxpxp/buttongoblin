#define main app_main
#include "buttongoblin-mk2.c"
#undef main
#include <assert.h>
static int events[128], event_count;
static void collect(int key, double t) {
  (void)t;
  events[event_count++] = key;
}
static void test_inputs(void) {
  cfg = newconfig();
  reset_usb_state();
  uint8_t state[15] = {0};
  event_count = 0;
  /* A device with no startup idle report must accept the first fresh press. */
  state[0] = 1;
  handle_input(state, 1, collect);
  assert(event_count == 1 && events[0] == 0);
  handle_input(state, 1.1, collect);
  assert(event_count == 1);
  state[0] = 0;
  handle_input(state, 1.2, collect);
  memset(state, 1, 15);
  handle_input(state, 2, collect);
  assert(event_count == 16);
  for (int k = 0; k < 15; k++)
    assert(events[k + 1] == k);
  reset_usb_state();
  event_count = 0;
  memset(state, 0, 15);
  cfg->keys[1].trigger = RELEASE;
  state[1] = 1;
  handle_input(state, 3, collect);
  assert(event_count == 0);
  state[1] = 0;
  handle_input(state, 3.1, collect);
  assert(event_count == 1 && events[0] == 1);
  reset_usb_state();
  event_count = 0;
  cfg->keys[2].trigger = HOLD;
  cfg->keys[2].hold_ms = 600;
  state[2] = 1;
  handle_input(state, 4, collect);
  handle_holds(4.5, collect);
  assert(event_count == 0);
  handle_holds(4.7, collect);
  handle_holds(5, collect);
  assert(event_count == 1 && events[0] == 2);
  state[2] = 0;
  handle_input(state, 5.1, collect);
  assert(event_count == 1);
  state[2] = 1;
  handle_input(state, 6, collect);
  state[2] = 0;
  handle_input(state, 6.8, collect);
  assert(event_count == 2); /* threshold crossed before release processing */
  state[2] = 1;
  handle_input(state, 7, collect);
  state[2] = 0;
  handle_input(state, 7.2, collect);
  assert(event_count == 2);
  reset_usb_state();
  event_count = 0;
  inputs.armed[0] = 0;
  inputs.down[0] = 1;
  memset(state, 0, 15);
  state[0] = 1;
  handle_input(state, 8, collect);
  assert(event_count == 0);
  state[0] = 0;
  handle_input(state, 8.1, collect);
  assert(event_count == 0);
  state[0] = 1;
  handle_input(state, 8.2, collect);
  assert(event_count == 1);
  reset_usb_state();
  event_count = 0;
  memset(state, 0, 15);
  state[0] = 1;
  handle_input(state, 9, collect);
  assert(event_count == 1);
  reset_after_reload();
  handle_input(state, 9.1, collect);
  assert(event_count == 1);
  state[0] = 0;
  handle_input(state, 9.2, collect);
  assert(event_count == 1);
  state[0] = 1;
  handle_input(state, 9.3, collect);
  assert(event_count == 2);
  struct queue q = {0};
  for (int i = 0; i < QCAP; i++)
    assert(enqueue(&q, i % 15) == 0);
  assert(enqueue(&q, 1) == -1);
  for (int i = 0; i < QCAP; i++)
    assert(dequeue_at(&q, 0) == i % 15);
  assert(dequeue_at(&q, 0) == -1);
  assert(enqueue(&q, 9) == 0 && dequeue_at(&q, 0) == 9);
  assert(enqueue(&q, 0) == 0 && enqueue(&q, 1) == 0 && enqueue(&q, 13) == 0);
  assert(select_action(cfg, &q, 1, 0) == 2);
  assert(dequeue_at(&q, 2) == 13 && q.count == 2);
  assert(select_action(cfg, &q, 2, 0) == -1);
  assert(select_action(cfg, &q, 3, 0) == 0);
  assert(dequeue_at(&q, 0) == 0 && dequeue_at(&q, 0) == 1);

  freeconfig(cfg);
  cfg = NULL;
  puts("PASS: first press, all 15 simultaneous edges, repeat suppression, "
       "release, hold, reconnect rearming, queue FIFO/capacity");
}
static void test_upload(void) {
  cfg = newconfig();
  for (int k = 0; k < 15; k++) {
    uploads[k].current = 0;
    uploads[k].finished = 1;
    uploads[k].wanted = 0;
    uploads[k].offset = 0;
  }
  struct image *im = &cfg->keys[3].images[0];
  im->size = 2100;
  im->data = malloc(im->size);
  for (size_t i = 0; i < im->size; i++)
    im->data[i] = (uint8_t)i;
  uploads[3].current = -1;
  int p[2];
  assert(pipe(p) == 0);
  uint8_t b[1024];
  size_t done = 0;
  for (int chunk = 0; chunk < 3; chunk++) {
    assert(upload_step(p[1]) == 1);
    assert(read(p[0], b, sizeof b) == 1024);
    size_t n = b[4] + ((size_t)b[5] << 8);
    assert(b[2] == 3 && b[6] == chunk && b[3] == (chunk == 2));
    assert(memcmp(b + 8, im->data + done, n) == 0);
    done += n;
  }
  assert(done == 2100 && upload_step(p[1]) == 0);
  assert(fcntl(p[1], F_SETFL, O_NONBLOCK) == 0);
  while (write(p[1], b, sizeof b) == 1024) {
  }
  assert(errno == EAGAIN);
  uploads[3].current = -1;
  upload_wait_since = 0;
  assert(upload_step(p[1]) == 0 && upload_wait_since > 0);
  upload_wait_since = nowtime() - 3;
  assert(upload_step(p[1]) == -1);
  assert(fcntl(p[0], F_SETFL, O_NONBLOCK) == 0);
  while (read(p[0], b, sizeof b) > 0) {
  }
  assert(errno == EAGAIN);
  assert(upload_step(p[1]) == 1);
  assert(read(p[0], b, sizeof b) == 1024 && b[6] == 0 && b[3] == 0);
  assert(!memcmp(b + 8, im->data, 1016));

  /* Firmware assembles one JPEG at a time across all keys. A newly due
     lower-index key must not steal the middle of another key's image. */
  reset_usb_state();
  for (int k = 0; k < KEYS; k++) {
    uploads[k].current = 0;
    uploads[k].finished = 1;
  }
  struct image *other = &cfg->keys[1].images[0];
  other->size = 1200;
  other->data = malloc(other->size);
  memset(other->data, 0x6b, other->size);
  struct image *feedback = &cfg->keys[3].images[1];
  feedback->size = 1200;
  feedback->data = malloc(feedback->size);
  memset(feedback->data, 0x9c, feedback->size);
  next_upload_key = 1;
  uploads[3].finished = 0;
  assert(upload_step(p[1]) == 1);
  assert(read(p[0], b, sizeof b) == 1024 && b[2] == 3 && b[6] == 0);
  uploads[1].finished = 0; /* another animation becomes due mid-upload */
  visual(3, 1, nowtime()); /* press switches the current key mid-upload */
  for (int chunk = 1; chunk < 3; chunk++) {
    assert(upload_step(p[1]) == 1);
    assert(read(p[0], b, sizeof b) == 1024 && b[2] == 3 && b[6] == chunk &&
           b[3] == (chunk == 2));
    assert(!memcmp(b + 8, im->data + chunk * 1016, chunk == 1 ? 1016 : 68));
  }
  assert(active_upload_key == -1);
  /* Press feedback wins at the next image boundary, never mid-image. */
  assert(upload_step(p[1]) == 1);
  assert(read(p[0], b, sizeof b) == 1024 && b[2] == 3 && b[6] == 0 &&
         b[8] == 0x9c);
  assert(upload_step(p[1]) == 1);
  assert(read(p[0], b, sizeof b) == 1024 && b[2] == 3 && b[6] == 1 && b[3]);
  assert(upload_step(p[1]) == 1);
  assert(read(p[0], b, sizeof b) == 1024 && b[2] == 1 && b[6] == 0);
  assert(upload_step(p[1]) == 1);
  assert(read(p[0], b, sizeof b) == 1024 && b[2] == 1 && b[6] == 1 && b[3]);
  assert(upload_step(p[1]) == 0);
  reset_usb_state();
  assert(active_upload_key == -1 && next_upload_key == 0);
  close(p[0]);
  close(p[1]);
  freeconfig(cfg);
  cfg = NULL;
  puts(
      "PASS: complete JPEG serialization across keys and feedback transitions");
}
static void test_command(void) {
  char path[PATHCAP], buffer[1200];
  assert(runtimepath(path, ".fifo") == 0);
  assert(mkfifo(path, 0600) == 0);
  double start = nowtime();
  assert(command_write(path, "/media/fat/a.mgl") == -1);
  assert(nowtime() - start < 0.2);
  int reader = open(path, O_RDONLY | O_NONBLOCK);
  assert(reader >= 0);
  assert(command_write(path, "/media/fat/A Game.MGL") == 0);
  ssize_t n = read(reader, buffer, sizeof buffer);
  assert(n > 0);
  buffer[n] = 0;
  assert(!strcmp(buffer, "load_core /media/fat/A Game.MGL\n"));
  assert(command_write(path, "/media/fat/bad.sfc") == -1);
  assert(command_write(path, "/media/fat/bad\n.mgl") == -1);
  close(reader);
  unlink(path);
  assert(command_write(path, "/media/fat/a.rbf") == -1);
  assert(access(path, F_OK) != 0);
  FILE *f = fopen(path, "w");
  assert(f);
  fputs("untouched", f);
  fclose(f);
  assert(command_write(path, "/media/fat/a.rbf") == -1);
  f = fopen(path, "r");
  assert(f);
  assert(fgets(buffer, sizeof buffer, f));
  assert(!strcmp(buffer, "untouched"));
  fclose(f);
  unlink(path);
  puts("PASS: FIFO command send, no-reader timeout, raw-ROM rejection, control "
       "characters, no creation/truncation of command endpoint");
}
static void test_scripts(void) {
  cfg = page_configs[0] = newconfig();
  active_page = 0;
  snprintf(base, sizeof base, "/tmp");
  char path[PATHCAP];
  assert(runtimepath(path, " weird;name.sh") == 0);
  cfg->keys[0].action = SCRIPT;
  snprintf(cfg->keys[0].target, PATHCAP, "%s", path);
  FILE *f = fopen(path, "w");
  assert(f);
  fputs("exit 0\n", f);
  fclose(f);
  assert(start_script(0, -1, -1) == 0);
  assert(start_script(0, -1, -1) == -1);
  double deadline = nowtime() + 2;
  while (script_pid > 0 && nowtime() < deadline) {
    reap_script(nowtime(), 0);
    poll(NULL, 0, 10);
  }
  assert(script_pid == -1 && uploads[0].wanted == 1);
  f = fopen(path, "w");
  assert(f);
  fputs("exit 7\n", f);
  fclose(f);
  assert(start_script(0, -1, -1) == 0);
  deadline = nowtime() + 2;
  while (script_pid > 0 && nowtime() < deadline) {
    reap_script(nowtime(), 0);
    poll(NULL, 0, 10);
  }
  assert(script_pid == -1 && uploads[0].wanted == 2);
  f = fopen(path, "w");
  assert(f);
  fputs("sleep 30\n", f);
  fclose(f);
  assert(start_script(0, -1, -1) == 0);
  poll(NULL, 0, 50);
  script_started -= 61;
  deadline = nowtime() + 2;
  while (script_pid > 0 && nowtime() < deadline) {
    reap_script(nowtime(), 0);
    poll(NULL, 0, 10);
  }
  assert(script_pid == -1 && uploads[0].wanted == 2);
  /* Update All survives the ordinary deadline but is still bounded/guarded. */
  cfg->keys[0].action = UPDATE_ALL;
  assert(start_script(0, -1, -1) == 0);
  assert(script_timeout_seconds == 3600);
  script_started -= 61;
  reap_script(nowtime(), 0);
  assert(script_pid > 0);
  reset_usb_state();
  assert(uploads[0].wanted == 1 && restore[0] == 0);
  reset_after_reload();
  assert(uploads[0].wanted == 1 && restore[0] == 0);
  script_started -= 3600;
  deadline = nowtime() + 2;
  while (script_pid > 0 && nowtime() < deadline) {
    reap_script(nowtime(), 0);
    poll(NULL, 0, 10);
  }
  assert(script_pid == -1 && uploads[0].wanted == 2);
  /* A successful non-interactive updater selects finished feedback. */
  f = fopen(path, "w");
  assert(f);
  fputs("[ ! -t 0 ] && [ \"$HOME\" = /root ] && [ \"$TERM\" = linux ] && [ "
        "\"$PWD\" = /tmp ] && [ \"$UPDATE_ALL_NON_INTERACTIVE\" = true ]\n",
        f);
  fclose(f);
  assert(start_script(0, -1, -1) == 0);
  visual(0, 1, nowtime());
  restore[0] = 0;
  deadline = nowtime() + 2;
  while (script_pid > 0 && nowtime() < deadline) {
    reap_script(nowtime(), 0);
    poll(NULL, 0, 10);
  }
  assert(script_pid == -1 && uploads[0].wanted == 3 && restore[0] == 0);
  reset_usb_state();
  assert(uploads[0].wanted == 3 && restore[0] == 0);
  reset_after_reload();
  assert(uploads[0].wanted == 3 && restore[0] == 0);
  /* A reassigned key must not receive status from its previous job. */
  f = fopen(path, "w");
  assert(f);
  fputs("sleep 0.1; exit 0\n", f);
  fclose(f);
  assert(start_script(0, -1, -1) == 0);
  strcpy(cfg->keys[0].target, "/tmp/different_update.sh");
  reset_after_reload();
  assert(uploads[0].wanted == 0);
  strcpy(cfg->keys[0].target, path);
  reset_after_reload();
  assert(uploads[0].wanted == 1 && restore[0] == 0);
  cfg->keys[0].action = CYCLE;
  reset_after_reload();
  assert(uploads[0].wanted == 0);
  deadline = nowtime() + 2;
  while (script_pid > 0 && nowtime() < deadline) {
    reap_script(nowtime(), 0);
    poll(NULL, 0, 10);
  }
  assert(script_pid == -1 && uploads[0].wanted == 0);
  cfg->keys[0].action = SCRIPT;
  /* Capped logging must not impose a size limit on script-created files. */
  char data_path[PATHCAP], log_path[PATHCAP];
  assert(runtimepath(data_path, ".script_data") == 0);
  assert(runtimepath(log_path, ".script.log") == 0);
  f = fopen(path, "w");
  assert(f);
  fprintf(f, "dd if=/dev/zero of='%s' bs=1024 count=256 2>/dev/null\n",
          data_path);
  fputs("dd if=/dev/zero bs=1024 count=256 2>/dev/null\nprintf DONE\nexit 0\n",
        f);
  fclose(f);
  assert(start_script(0, -1, -1) == 0);
  deadline = nowtime() + 4;
  while (script_pid > 0 && nowtime() < deadline) {
    reap_script(nowtime(), 0);
    poll(NULL, 0, 10);
  }
  assert(script_pid == -1 && uploads[0].wanted == 1);
  struct stat st;
  assert(stat(data_path, &st) == 0 && st.st_size == 262144);
  assert(stat(log_path, &st) == 0 && st.st_size > 0 &&
         st.st_size <= SCRIPT_LOG_CAP);
  FILE *recent = fopen(log_path, "rb");
  assert(recent && fseek(recent, -4, SEEK_END) == 0);
  char tail[4];
  assert(fread(tail, 1, sizeof tail, recent) == sizeof tail);
  assert(!memcmp(tail, "DONE", 4));
  fclose(recent);
  unlink(data_path);
  /* Closing stdout must not cause an EOF spin or lose the exit notification. */
  f = fopen(path, "w");
  assert(f);
  fputs("exec 1>&- 2>&-\nsleep 0.1\nexit 0\n", f);
  fclose(f);
  assert(start_script(0, -1, -1) == 0);
  deadline = nowtime() + 2;
  while (script_pid > 0 && nowtime() < deadline) {
    reap_script(nowtime(), 0);
    poll(NULL, 0, 10);
  }
  assert(script_pid == -1 && uploads[0].wanted == 1);
  assert(prctl(PR_SET_CHILD_SUBREAPER, 1) == 0);
  f = fopen(path, "w");
  assert(f);
  fputs("sleep 30 &\nwait\n", f);
  fclose(f);
  int pipefd[2];
  assert(pipe(pipefd) == 0);
  pid_t owner = fork();
  assert(owner >= 0);
  if (owner == 0) {
    close(pipefd[0]);
    assert(start_script(0, -1, -1) == 0);
    assert(write(pipefd[1], &script_pid, sizeof script_pid) ==
           sizeof script_pid);
    close(pipefd[1]);
    for (;;)
      pause();
  }
  close(pipefd[1]);
  pid_t group;
  assert(read(pipefd[0], &group, sizeof group) == sizeof group);
  close(pipefd[0]);
  poll(NULL, 0, 100);
  assert(kill(owner, SIGKILL) == 0);
  assert(waitpid(owner, NULL, 0) == owner);
  deadline = nowtime() + 2;
  while (nowtime() < deadline) {
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
    if (kill(-group, 0) < 0 && errno == ESRCH)
      break;
    poll(NULL, 0, 10);
  }
  assert(kill(-group, 0) < 0 && errno == ESRCH);
  unlink(path);
  freeconfig(cfg);
  cfg = NULL;
  puts("PASS: script literal path, singleton, exit result, timeout and "
       "worker-death group cleanup");
}
static void test_efficiency(void) {
  cfg = newconfig();
  reset_usb_state();
  for (int k = 0; k < KEYS; k++) {
    uploads[k].current = 0;
    uploads[k].finished = 1;
  }
  assert(worker_wait_ms(10, 1, 12, 11, 40, 10, -100) == 1000);
  assert(worker_wait_ms(10, 0, 10.3, 11, 40, 10, -100) >= 299);
  assert(worker_wait_ms(10, 0, 10.3, 11, 40, 10, -100) <= 301);
  cfg->keys[0].trigger = HOLD;
  cfg->keys[0].hold_ms = 700;
  inputs.down[0] = 1;
  inputs.since[0] = 9.5;
  int n = worker_wait_ms(10, 1, 12, 11, 40, 10, -100);
  assert(n >= 199 && n <= 201);
  inputs.held[0] = 1;
  assert(worker_wait_ms(10, 1, 12, 11, 40, 10, -100) == 1000);
  restore[1] = 10.2;
  n = worker_wait_ms(10, 1, 12, 11, 40, 10, -100);
  assert(n >= 199 && n <= 201);
  restore[1] = 0;
  uploads[2].wanted = 1;
  n = worker_wait_ms(10, 1, 12, 11, 40, 10.004, -100);
  assert(n >= 3 && n <= 5);
  uploads[2].wanted = 0;
  memset(&queue, 0, sizeof queue);
  assert(enqueue(&queue, 1) == 0);
  n = worker_wait_ms(10, 1, 12, 11, 40, 10, 7.5);
  assert(n >= 499 && n <= 501);
  assert(enqueue(&queue, 13) == 0);
  assert(worker_wait_ms(10, 1, 12, 11, 40, 10, 7.5) == 0);
  memset(&queue, 0, sizeof queue);
  script_pid = 123;
  script_started = -49.6;
  next_script_poll = 10.1;
  n = worker_wait_ms(10, 0, 12, 11, 40, 10, -100);
  assert(n >= 99 && n <= 101);
  script_started = -51;
  assert(worker_wait_ms(10, 0, 12, 11, 40, 10, -100) > 0);
  script_pid = -1;
  int pipefd[2];
  assert(pipe(pipefd) == 0);
  pid_t writer = fork();
  assert(writer >= 0);
  if (writer == 0) {
    close(pipefd[0]);
    poll(NULL, 0, 20);
    assert(write(pipefd[1], "x", 1) == 1);
    _exit(0);
  }
  close(pipefd[1]);
  struct pollfd fd = {pipefd[0], POLLIN, 0};
  double begin = nowtime();
  assert(poll(&fd, 1, 1000) == 1);
  assert(fd.revents & POLLIN);
  assert(nowtime() - begin < 0.5);
  close(pipefd[0]);
  assert(waitpid(writer, NULL, 0) == writer);
  pid_t child = fork();
  assert(child >= 0);
  if (child == 0) {
    background_priority();
    errno = 0;
    assert(getpriority(PRIO_PROCESS, 0) == 19 && errno == 0);
#ifdef SYS_ioprio_get
    int priority = syscall(SYS_ioprio_get, 1, 0);
    assert(priority >= 0 && priority >> 13 == 3);
#endif
    _exit(0);
  }
  int status;
  assert(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
         WEXITSTATUS(status) == 0);
  freeconfig(cfg);
  cfg = NULL;
  puts("PASS: idle/deadline waits, immediate input wake, CPU and disk "
       "background priority");
}
static void test_animation(const char *path) {
  FILE *file = fopen(path, "rb");
  assert(file);
  assert(fseek(file, 0, SEEK_END) == 0);
  long length = ftell(file);
  rewind(file);
  uint8_t *data = malloc((size_t)length);
  assert(data);
  assert(fread(data, 1, (size_t)length, file) == (size_t)length);
  fclose(file);
  cfg = newconfig();
  struct image *im = &cfg->keys[0].images[0];
  assert(parse_animation(im, data, (size_t)length) == 0 && im->frame_count > 1);
  struct image *status = &cfg->keys[0].images[1];
  status->size = im->frames[0].size;
  status->data = malloc(status->size);
  assert(status->data);
  memcpy(status->data, im->frames[0].data, status->size);
  reset_usb_state();
  for (int k = 1; k < KEYS; k++) {
    uploads[k].current = 0;
    uploads[k].finished = 1;
  }
  int pipefd[2];
  assert(pipe(pipefd) == 0);
  uint8_t packetdata[1024], received[IMAGECAP];
  for (unsigned frame = 0; frame < 2; frame++) {
    size_t offset = 0;
    unsigned chunk = 0;
    while (!uploads[0].finished) {
      assert(upload_step(pipefd[1]) == 1);
      assert(read(pipefd[0], packetdata, 1024) == 1024);
      size_t n = packetdata[4] | ((size_t)packetdata[5] << 8);
      assert(packetdata[2] == 0 &&
             (packetdata[6] | ((unsigned)packetdata[7] << 8)) == chunk++);
      assert(offset + n <= sizeof received);
      memcpy(received + offset, packetdata + 8, n);
      offset += n;
      assert((packetdata[3] != 0) == (offset == im->frames[frame].size));
    }
    assert(offset == im->frames[frame].size &&
           !memcmp(received, im->frames[frame].data, offset));
    double due = uploads[0].frame_due;
    assert(due > 0);
    advance_animations(due - .001);
    assert(uploads[0].finished);
    assert(worker_wait_ms(due - .1, 1, 0, due + 1, due + 1, due + 1, -100) <=
           101);
    advance_animations(due);
    assert(!uploads[0].finished &&
           uploads[0].frame == (frame + 1) % im->frame_count);
  }
  visual(0, 1, nowtime());
  assert(upload_step(pipefd[1]) == 1);
  assert(read(pipefd[0], packetdata, 1024) == 1024);
  assert(uploads[0].frame == 0 && uploads[0].current == 1);
  close(pipefd[0]);
  close(pipefd[1]);
  freeconfig(cfg);
  cfg = NULL;
  puts("PASS: animation cache, frame timing/chunks, no mid-frame advance, "
       "state reset");
}
static int closed_heartbeat_worker(void) {
  close(heartbeat_fd);
  heartbeat_fd = -1;
  while (running)
    pause();
  return 0;
}
static int crash_worker(void) { _exit(17); }
static int hang_worker(void) {
  signal(SIGTERM, SIG_IGN);
  for (;;)
    pause();
  return 0;
}
int main(int argc, char **argv) {
  if (argc == 1) {
    /* Mixed FIFO/control removals must remain correct after repeated wraps. */
    struct queue ring = {0};
    int model[QCAP], count = 0, wrapped = 0;
    for (unsigned step=0;step<10000;step++) {
      if (count<QCAP && step%3) {
        int key=(step*7)%KEYS;
        assert(enqueue(&ring,key)==0); model[count++]=key;
      } else if (count) {
        int index=step%5 ? 0 : count/2;
        assert(dequeue_at(&ring,index)==model[index]);
        memmove(model+index,model+index+1,(count-index-1)*sizeof *model);
        count--;
      }
      if (ring.head) wrapped=1;
      assert(ring.count==count);
      for (int i=0;i<count;i++) assert(ring.keys[(ring.head+i)%QCAP]==model[i]);
    }
    assert(wrapped);
    puts("PASS: 10000 mixed FIFO/control removals across ring-buffer wraps");
    for (int current = 0; current <= 100; current++) {
      int next = next_brightness(CYCLE, current);
      int expected = current < 10 ? 10 : current < 25 ? 25 :
                     current < 50 ? 50 : current < 75 ? 75 : current < 100 ? 100 : 10;
      assert(next == expected);
      assert(next >= 10 && next <= 100);
    }
    int level = 100;
    const int sequence[] = {10, 25, 50, 75, 100};
    for (unsigned i=0; i<5; i++) { level=next_brightness(CYCLE,level); assert(level==sequence[i]); }
    assert(next_brightness(CYCLE,65)==75);
    assert(next_brightness(DIM, 5) == 0);
    assert(next_brightness(BRIGHT, 95) == 100);
    assert(valid_action_target(UPDATE_ALL, "Scripts/update_all.sh"));
    assert(!valid_action_target(UPDATE_ALL, "bad.exe"));
    struct config *defaults_test = newconfig();
    assert(defaults_test && defaults_test->keys[13].action == UPDATE_ALL);
    assert(!strcmp(defaults_test->keys[13].target, "Scripts/update_all.sh"));
    assert(defaults_test->keys[12].action == LAUNCH);
    assert(!strcmp(defaults_test->keys[12].target,
                   "_Arcade/The Simpsons (4 Players World, set 1).mra"));
    assert(defaults_test->keys[14].action == CYCLE);
    freeconfig(defaults_test);
    puts("PASS: five-level ascending brightness cycle, boundary wrap, updater "
         "defaults/targets");
    test_efficiency();
    test_inputs();
    test_upload();
    return selftest();
  }
  if (argc != 3)
    return 2;
  snprintf(runtime, sizeof runtime, "%s", argv[2]);
  if (!strcmp(argv[1], "animation")) {
    test_animation(argv[2]);
    return 0;
  }
  if (!strcmp(argv[1], "script")) {
    test_scripts();
    return 0;
  }
  if (!strcmp(argv[1], "command")) {
    test_command();
    return 0;
  }
  if (!strcmp(argv[1], "closed"))
    return supervise(closed_heartbeat_worker, 20000);
  if (!strcmp(argv[1], "crash"))
    return supervise(crash_worker, 500);
  if (!strcmp(argv[1], "hang"))
    return supervise(hang_worker, 300);
  return 2;
}
