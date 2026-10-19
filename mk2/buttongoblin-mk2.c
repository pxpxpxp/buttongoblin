#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <errno.h>
#include <linux/uinput.h>
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define VERSION "Button Goblin MK.2 2.0.0"
#define KEYS 15
#define PAGES 2
#define NAV_PAGE 1
#define BRIGHTNESS_KEY 14
#define PATHCAP 2048
#define IMAGECAP 32768
#define QCAP 32
#define ANIMCAP 524288
#define CACHECAP 4194304
#define FRAMECAP 32
#define UPLOAD_INTERVAL_MS 4
#define STATUS_INTERVAL 30
#define SCRIPT_TIMEOUT_SECONDS 60
#define SCRIPT_LOG_CAP 131072
struct rawinfo {
  unsigned int bustype;
  short vendor, product;
};
#define RAWINFO _IOR('H', 3, struct rawinfo)
#define FEATURE(n) _IOC(_IOC_WRITE | _IOC_READ, 'H', 6, n)
enum { NONE, LAUNCH, DIM, BRIGHT, RELOAD, RESTART, SCRIPT, CYCLE, UPDATE_ALL, SPACE_BUTTON, MENU_BUTTON, ENTER_BUTTON, UP_BUTTON, DOWN_BUTTON, LEFT_BUTTON, RIGHT_BUTTON, ESC_BUTTON, BACK_PAGE };
enum { PRESS, RELEASE, HOLD };
struct frame {
  const uint8_t *data;
  size_t size;
  unsigned duration_ms;
};
/* Static JPEG data is owned directly; animated data points into storage.
 * freeconfig releases the owner once, then the separate frame descriptors. */
struct image {
  uint8_t *data;
  size_t size;
  uint8_t *storage;
  struct frame *frames;
  unsigned frame_count;
  int borrowed; /* Same-config alias; only the first image owns its buffers. */
};
struct key {
  char label[96], target[PATHCAP];
  char *paths[4];
  int action, trigger, hold_ms;
  struct image images[4];
};
struct config {
  int brightness, gap_ms;
  struct key keys[KEYS];
};
struct inputs {
  uint8_t down[KEYS], held[KEYS], armed[KEYS];
  double since[KEYS];
};
struct queue {
  int keys[QCAP], head, count;
};
struct upload {
  int wanted, current, finished;
  size_t offset;
  unsigned chunk, frame;
  double frame_due;
};
static char base[PATHCAP] = "/media/fat", assets[PATHCAP], configpath[PATHCAP],
            runtime[PATHCAP] = "/tmp/buttongoblin";
static volatile sig_atomic_t running = 1, reloading = 0;
static volatile sig_atomic_t child_changed = 0;
static int heartbeat_fd = -1;
static struct config *cfg;
static struct config *startup_cfg;
static struct config *page_configs[2];
static int active_page, page_transition;
static struct upload page_uploads[2][KEYS];
static double page_restore[2][KEYS];
static void switch_page(int page);
static int startup_stage, startup_shown;
static double startup_since;
static int startup_level = -1;
static struct queue queue;
static struct inputs inputs;
static struct upload uploads[KEYS];
static double restore[KEYS];
static double upload_wait_since = 0;
static int active_upload_key = -1, next_upload_key = 0;
static unsigned long input_count = 0, queue_overflow = 0, launch_count = 0,
                     usb_reconnects = 0, invalid_reports = 0;
static const char *labels[KEYS] = {
    "NES",          "SNES",   "Genesis",  "TG16",       "PlayStation",
    "Saturn",       "N64",    "MegaCD",   "GBA",        "GameBoy",
    "MasterSystem", "NeoGeo", "Simpsons", "Update All", "Brightness"};
static const char *defaults[13] = {
    "_Console/NES_*.rbf",
    "_Console/SNES_*.rbf",
    "_Console/MegaDrive_*.rbf",
    "_Console/TurboGrafx16_*.rbf",
    "_Console/PSX_*.rbf",
    "_Console/Saturn_*.rbf",
    "_Console/N64_*.rbf",
    "_Console/MegaCD_*.rbf",
    "_Console/GBA_*.rbf",
    "_Console/Gameboy_*.rbf",
    "_Console/SMS_*.rbf",
    "_Console/NeoGeo_*.rbf",
    "_Arcade/The Simpsons (4 Players World, set 1).mra"};
static double nowtime(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}
static void stop(int s) {
  (void)s;
  running = 0;
}
static void reload_signal(int s) {
  (void)s;
  reloading = 1;
}
static int join(char *out, size_t cap, const char *a, const char *b) {
  int n = snprintf(out, cap, "%s/%s", a, b);
  return n < 0 || (size_t)n >= cap ? -1 : 0;
}
static int runtimepath(char out[PATHCAP], const char *suffix) {
  size_t a = strlen(runtime), b = strlen(suffix);
  if (a + b >= PATHCAP)
    return -1;
  memcpy(out, runtime, a);
  memcpy(out + a, suffix, b + 1);
  return 0;
}
static void logmsg(const char *fmt, ...) {
  char p[PATHCAP], old[PATHCAP];
  if (runtimepath(p, ".log") || runtimepath(old, ".log.old"))
    return;
  struct stat st;
  if (!stat(p, &st) && st.st_size > 131072)
    rename(p, old);
  FILE *f = fopen(p, "a");
  if (!f)
    return;
  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  char ts[32];
  strftime(ts, sizeof ts, "%F %T", &t);
  fprintf(f, "%s ", ts);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
  fclose(f);
}
/* Runtime ownership: the supervisor holds the service lock, forks one worker,
 * and watches its heartbeat. The worker owns HID, cached images and scripts.
 * No rendering, filesystem reads or shell polling occur in the upload loop. */
/* Only this service and its descendants are demoted. Never alter MiSTer. */
static void background_priority(void) {
  if (setpriority(PRIO_PROCESS, 0, 19))
    logmsg("Cannot set background CPU priority: %s", strerror(errno));
#ifdef SYS_ioprio_set
  /* Linux IOPRIO_WHO_PROCESS=1, IOPRIO_CLASS_IDLE=3, class shift=13. */
  if (syscall(SYS_ioprio_set, 1, 0, 3 << 13) < 0)
    logmsg("Idle disk priority unavailable: %s", strerror(errno));
#endif
}
static void child_signal(int signal_number) { (void)signal_number; child_changed=1; }
static void earlier(double *deadline, double value) {
  if (value < *deadline)
    *deadline = value;
}
static int timeout_ms(double now, double deadline) {
  double ms = (deadline - now) * 1000;
  if (ms <= 0)
    return 0;
  if (ms >= 1000)
    return 1000;
  int rounded = (int)ms;
  return rounded + (ms > rounded);
}
static int regular(const char *p) {
  struct stat st;
  return !stat(p, &st) && S_ISREG(st.st_mode);
}
static int valid_target(const char *p) {
  const char *s = strrchr(p, '.');
  return s && (!strcasecmp(s, ".rbf") || !strcasecmp(s, ".mgl") ||
               !strcasecmp(s, ".mra"));
}
static int cleanpath(const char *p) {
  if (!*p)
    return 0;
  for (; *p; p++)
    if ((unsigned char)*p < 32 || *p == 127)
      return 0;
  return 1;
}
static int valid_action_target(int action, const char *p) {
  if (action == LAUNCH)
    return valid_target(p);
  const char *dot = strrchr(p, '.');
  return (action == SCRIPT || action == UPDATE_ALL) && cleanpath(p) && dot &&
         !strcasecmp(dot, ".sh");
}
static int resolve(const struct config *c, int key, char out[PATHCAP]) {
  if (key < 0 || key >= KEYS ||
      (c->keys[key].action != LAUNCH && c->keys[key].action != SCRIPT &&
       c->keys[key].action != UPDATE_ALL))
    return -1;
  const char *target = c->keys[key].target;
  char pat[PATHCAP];
  out[0] = 0;
  if (target[0] == '/') {
    if (strlen(target) >= sizeof pat)
      return -1;
    strcpy(pat, target);
  } else if (join(pat, sizeof pat, base, target))
    return -1;
  /* Exact paths win before globbing: [USA], ? and * may be literal filename
     characters, particularly in game launcher titles. */
  if (strlen(pat) <= 1000 && cleanpath(pat) &&
      valid_action_target(c->keys[key].action, pat) && regular(pat)) {
    strcpy(out, pat);
    return 0;
  }
  glob_t g = {0};
  int rc = glob(pat, 0, NULL, &g);
  if (!rc)
    for (size_t i = 0; i < g.gl_pathc; i++) {
      const char *p = g.gl_pathv[i];
      if (strlen(p) > 1000 || !cleanpath(p) ||
          !valid_action_target(c->keys[key].action, p) || !regular(p))
        continue;
      if (!out[0] || strcmp(p, out) > 0)
        strcpy(out, p);
    }
  globfree(&g);
  if (!out[0]) {
    char *u = strstr(pat, "_*.rbf");
    if (u && u[6] == 0) {
      strcpy(u, ".rbf");
      if (strlen(pat) <= 1000 && cleanpath(pat) && regular(pat))
        strcpy(out, pat);
    }
  }
  return out[0] ? 0 : -1;
}
static char *trim(char *s) {
  while (*s == ' ' || *s == '\t')
    s++;
  char *e = s + strlen(s);
  while (e > s &&
         (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
    *--e = 0;
  return s;
}
static int number(const char *s, int lo, int hi, int *out) {
  char *e;
  errno = 0;
  long n = strtol(s, &e, 10);
  if (!*s || *e || errno || n < lo || n > hi)
    return -1;
  *out = (int)n;
  return 0;
}
static int textset(char *out, size_t cap, const char *s) {
  if (strlen(s) >= cap || !cleanpath(s))
    return -1;
  strcpy(out, s);
  return 0;
}
static int enumset(const char *s, const char *const names[], int count,
                   int *out) {
  for (int i = 0; i < count; i++)
    if (!strcmp(s, names[i])) {
      *out = i;
      return 0;
    }
  return -1;
}
static void freeconfig(struct config *c);
/* Image paths are cold configuration data, not 60 permanently padded buffers. */
static int pathset(char **out, const char *value) {
  size_t n = strlen(value);
  if (n >= PATHCAP || !cleanpath(value)) return -1;
  char *copy = malloc(n + 1);
  if (!copy) return -1;
  memcpy(copy, value, n + 1);
  free(*out); *out = copy;
  return 0;
}
/* Stay Puft increases through five visible levels; dim/bright are also configurable. */
static int next_brightness(int action, int current) {
  if (action == CYCLE) {
    const int levels[] = {10, 25, 50, 75, 100};
    for (unsigned i = 0; i < sizeof levels / sizeof levels[0]; i++)
      if (levels[i] > current) return levels[i];
    return 10;
  }
  int level = current + (action == DIM ? -15 : 15);
  return level < 0 ? 0 : level > 100 ? 100 : level;
}
static struct config *newconfig(void) {
  struct config *c = calloc(1, sizeof *c);
  if (!c)
    return NULL;
  c->brightness = 65;
  c->gap_ms = 3000;
  for (int k = 0; k < KEYS; k++) {
    strcpy(c->keys[k].label, labels[k]);
    c->keys[k].action = k < 13 ? LAUNCH : k == 13 ? UPDATE_ALL : CYCLE;
    c->keys[k].trigger = PRESS;
    c->keys[k].hold_ms = 600;
    if (k < 13)
      strcpy(c->keys[k].target, defaults[k]);
    else if (k == 13)
      strcpy(c->keys[k].target, "Scripts/update_all.sh");
    const char *states[] = {"normal", "request", "error"};
    for (int s = 0; s < 3; s++) {
      char path[32];
      snprintf(path, sizeof path, "%02d_%s.jpg", k, states[s]);
      if (pathset(&c->keys[k].paths[s], path)) { freeconfig(c); return NULL; }
    }
    if (pathset(&c->keys[k].paths[3], "custom/update_all_finished_gray.sda")) {
      freeconfig(c); return NULL;
    }
  }
  return c;
}
static void freeconfig(struct config *c) {
  if (c) {
    for (int k = 0; k < KEYS; k++)
      for (int s = 0; s < 4; s++) {
        free(c->keys[k].paths[s]);
        struct image *im = &c->keys[k].images[s];
        if (!im->borrowed) {
          free(im->storage ? im->storage : im->data);
          free(im->frames);
        }
      }
    free(c);
  }
}
/* Opening a FIFO/device as configuration must never hang validation. */
static FILE *regular_input(const char *path, int config_file) {
  int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0)
    return NULL;
  struct stat st;
  if (fstat(fd, &st) || !S_ISREG(st.st_mode) ||
      (config_file && st.st_size > 131072)) {
    close(fd);
    errno = EINVAL;
    return NULL;
  }
  FILE *f = fdopen(fd, "rb");
  if (!f)
    close(fd);
  return f;
}
static int parseconfig(struct config *c, const char *path) {
  FILE *f = regular_input(path, 1);
  if (!f) {
    fprintf(stderr, "Cannot open config: %s\n", path);
    return -1;
  }
  char line[PATHCAP + 160];
  int lineno = 0, result = 0;
  int explicit_action[KEYS] = {0};
  for (;;) {
    memset(line, 0xff, sizeof line);
    if (!fgets(line, sizeof line, f))
      break;
    /* fgets permits embedded NULs; do not silently parse their prefix. */
    char *end = memrchr(line, 0, sizeof line);
    if (!end || memchr(line, 0, (size_t)(end - line))) {
      result = -1;
      break;
    }
    lineno++;
    if (!strchr(line, '\n') && !feof(f)) {
      result = -1;
      break;
    }
    char *name = trim(line);
    if (!*name || *name == '#')
      continue;
    char *value = strchr(name, '=');
    if (!value) {
      result = -1;
      break;
    }
    *value++ = 0;
    name = trim(name);
    value = trim(value);
    if (!strcmp(name, "brightness")) {
      result = number(value, 0, 100, &c->brightness);
    } else if (!strcmp(name, "launch_gap_ms")) {
      result = number(value, 100, 30000, &c->gap_ms);
    } else {
      int k;
      char *field = NULL;
      if (!strncmp(name, "key.", 4)) {
        char *dot = strchr(name + 4, '.');
        if (!dot) {
          result = -1;
          break;
        }
        *dot = 0;
        if (number(name + 4, 0, 14, &k)) {
          result = -1;
          break;
        }
        field = dot + 1;
      } else {
        if (number(name, 0, 14, &k)) {
          result = -1;
          break;
        }
        field = "target";
      }
      struct key *key = &c->keys[k];
      if (!strcmp(field, "target")) {
        result = textset(key->target, sizeof key->target, value);
        if (!result && !cleanpath(value))
          result = -1;
        if (!result && !explicit_action[k])
          key->action = LAUNCH;
      } else if (!strcmp(field, "label"))
        result = textset(key->label, sizeof key->label, value);
      else if (!strcmp(field, "image"))
        result = pathset(&key->paths[0], value);
      else if (!strcmp(field, "request_image"))
        result = pathset(&key->paths[1], value);
      else if (!strcmp(field, "error_image"))
        result = pathset(&key->paths[2], value);
      else if (!strcmp(field, "success_image"))
        result = pathset(&key->paths[3],
          !strcmp(value, "custom/update_all_finished.sda") ?
          "custom/update_all_finished_gray.sda" : value);
      else if (!strcmp(field, "hold_ms"))
        result = number(value, 100, 10000, &key->hold_ms);
      else if (!strcmp(field, "action")) {
        const char *names[] = {"none",   "launch", "dim",
                               "bright", "reload", "restart",
                               "script", "cycle",  "update_all", "space", "menu", "enter", "up", "down", "left", "right", "escape", "back"};
        result = enumset(value, names, 18, &key->action);
        explicit_action[k] = 1;
      } else if (!strcmp(field, "trigger")) {
        const char *names[] = {"press", "release", "hold"};
        result = enumset(value, names, 3, &key->trigger);
      } else
        result = -1;
    }
    if (result)
      break;
  }
  if (ferror(f))
    result = -1;
  fclose(f);
  if (result)
    fprintf(stderr, "Invalid configuration at line %d\n", lineno);
  if (!result)
    for (int k = 0; k < KEYS; k++)
      if ((c->keys[k].action == LAUNCH || c->keys[k].action == SCRIPT ||
           c->keys[k].action == UPDATE_ALL) &&
          !valid_action_target(c->keys[k].action, c->keys[k].target)) {
        fprintf(stderr, "Missing/invalid action target for key %d\n", k);
        result = -1;
      }
  return result;
}
/* Marker validation: bounded JPEG, 72x72, baseline or progressive SOF, and EOI.
   Full decoding happens on the deck; use prepare_image.py for known-good
   images. */
static int validjpeg(const uint8_t *p, size_t n) {
  if (n < 12 || p[0] != 255 || p[1] != 216 || p[n - 2] != 255 ||
      p[n - 1] != 217)
    return 0;
  size_t i = 2;
  while (i + 3 < n) {
    if (p[i++] != 255)
      return 0;
    while (i < n && p[i] == 255)
      i++;
    if (i >= n)
      return 0;
    int mark = p[i++];
    if (mark == 0xd9 || mark == 0xda)
      return 0;
    if (mark == 1 || (mark >= 0xd0 && mark <= 0xd7))
      continue;
    if (i + 2 > n)
      return 0;
    size_t length = ((size_t)p[i] << 8) | p[i + 1];
    if (length < 2 || length > n - i)
      return 0;
    if (mark == 0xc0 || mark == 0xc2) {
      if (length < 8)
        return 0;
      return p[i + 2] == 8 && p[i + 3] == 0 && p[i + 4] == 72 &&
             p[i + 5] == 0 && p[i + 6] == 72;
    }
    i += length;
  }
  return 0;
}
/* SDAN v1: magic, LE16 version/count; repeated LE32 duration/size + JPEG.
   GIF decoding happens on the computer, never on the MiSTer worker. */
static unsigned le16(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }
static uint32_t le32(const uint8_t *p) {
  return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
static int parse_animation(struct image *im, uint8_t *data, size_t size) {
  if (size < 8 || memcmp(data, "SDAN", 4) || le16(data + 4) != 1)
    return -1;
  unsigned count = le16(data + 6);
  if (!count || count > FRAMECAP)
    return -1;
  struct frame *frames = calloc(count, sizeof *frames);
  if (!frames)
    return -1;
  size_t offset = 8;
  unsigned duration = 0;
  for (unsigned f = 0; f < count; f++) {
    if (size - offset < 8) {
      free(frames);
      return -1;
    }
    unsigned delay = le32(data + offset);
    size_t n = le32(data + offset + 4);
    offset += 8;
    if (delay < 200 || delay > 30000 || n > IMAGECAP || n > size - offset ||
        !validjpeg(data + offset, n) || duration > 30000 - delay) {
      free(frames);
      return -1;
    }
    frames[f] = (struct frame){data + offset, n, delay};
    duration += delay;
    offset += n;
  }
  if (offset != size) {
    free(frames);
    return -1;
  }
  im->storage = data;
  im->frames = frames;
  im->frame_count = count;
  im->data = (uint8_t *)frames[0].data;
  im->size = size;
  return 0;
}
/* Build a complete replacement cache before publishing a reload. On any
 * failure readconfig frees this candidate and keeps the working cache. */
static int loadimages(struct config *c) {
  size_t total = 0;
  for (int k = 0; k < KEYS; k++)
    for (int state = 0; state < 4; state++) {
      if (state == 3 && c->keys[k].action != UPDATE_ALL) continue;
      char path[PATHCAP];
      const char *p = c->keys[k].paths[state];
      /* Reuse identical paths only inside this candidate config. Reloads still
       * load fresh bytes, and no alias can outlive its owner. */
      struct image *same = NULL;
      for (int prior = 0; prior < k * 4 + state; prior++) {
        int pk = prior / 4, ps = prior % 4;
        struct image *im = &c->keys[pk].images[ps];
        if (im->data && !strcmp(p, c->keys[pk].paths[ps])) { same = im; break; }
      }
      if (same) {
        c->keys[k].images[state] = *same;
        c->keys[k].images[state].borrowed = 1;
        continue;
      }
      if (p[0] == '/') {
        if (textset(path, sizeof path, p))
          return -1;
      } else if (join(path, sizeof path, assets, p))
        return -1;
      FILE *f = regular_input(path, 0);
      if (!f) {
        fprintf(stderr, "Missing key %d image: %s\n", k, path);
        return -1;
      }
      if (fseek(f, 0, SEEK_END)) {
        fclose(f);
        return -1;
      }
      long length = ftell(f);
      if (length < 12 || length > ANIMCAP ||
          (size_t)length > CACHECAP - total) {
        fclose(f);
        return -1;
      }
      rewind(f);
      size_t n = (size_t)length;
      uint8_t *data = malloc(n);
      if (!data) {
        fclose(f);
        return -1;
      }
      int ok = fread(data, 1, n, f) == n;
      fclose(f);
      struct image *im = &c->keys[k].images[state];
      if (ok && !memcmp(data, "SDAN", 4))
        ok = parse_animation(im, data, n) == 0;
      else if (ok) {
        ok = n <= IMAGECAP && validjpeg(data, n);
        if (ok) {
          im->data = data;
          im->size = n;
        }
      }
      if (!ok) {
        free(data);
        fprintf(stderr, "Invalid JPEG/animation: %s\n", path);
        return -1;
      }
      total += n;
    }
  return 0;
}
static struct config *readconfig(int images) {
  struct config *c = newconfig();
  if (!c)
    return NULL;
  if (parseconfig(c, configpath) || (images && loadimages(c))) {
    freeconfig(c);
    return NULL;
  }
  return c;
}
static int finddeck(char path[128]) {
  glob_t g = {0};
  if (glob("/dev/hidraw*", 0, NULL, &g)) {
    globfree(&g);
    return -1;
  }
  int chosen = -1;
  for (size_t i = 0; i < g.gl_pathc; i++) {
    int fd = open(g.gl_pathv[i], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
      continue;
    struct rawinfo info = {0};
    if (!ioctl(fd, RAWINFO, &info) && (uint16_t)info.vendor == 0x0fd9 &&
        ((uint16_t)info.product == 0x0080 ||
         (uint16_t)info.product == 0x00a5)) {
      close(fd);
      fd=open(g.gl_pathv[i],O_RDWR|O_NONBLOCK|O_CLOEXEC);
      if(fd<0)continue;
      if(flock(fd,LOCK_EX|LOCK_NB)||ioctl(fd,RAWINFO,&info)||info.bustype!=BUS_USB||
         (uint16_t)info.vendor!=0x0fd9||((uint16_t)info.product!=0x0080&&(uint16_t)info.product!=0x00a5)){
        close(fd);continue;
      }
      snprintf(path, 128, "%s", g.gl_pathv[i]);
      chosen = fd;
      break;
    }
    close(fd);
  }
  globfree(&g);
  return chosen;
}
static int feature(int fd, uint8_t command, int value) {
  uint8_t b[32] = {3, command};
  b[2] = (uint8_t)value;
  int rc;
  do {
    rc = ioctl(fd, FEATURE(sizeof b), b);
  } while (rc < 0 && errno == EINTR && running);
  return rc;
}
static void packet(uint8_t b[1024], int key, unsigned chunk, const uint8_t *p,
                   size_t n, int last) {
  memset(b, 0, 1024);
  b[0] = 2;
  b[1] = 7;
  b[2] = (uint8_t)key;
  b[3] = (uint8_t)last;
  b[4] = (uint8_t)n;
  b[5] = (uint8_t)(n >> 8);
  b[6] = (uint8_t)chunk;
  b[7] = (uint8_t)(chunk >> 8);
  memcpy(b + 8, p, n);
}
static void visual(int key, int state, double now) {
  uploads[key].wanted = state;
  restore[key] = state ? now + 2 : 0;
}
static void restore_script_visual(void);
static int splash_active(void) {
  return startup_cfg && (startup_stage == 1 || startup_stage == 2 ||
                         startup_stage == 3 || startup_stage == 6);
}
/* One persistent virtual keyboard: no subprocess, no per-press allocation.
 * Closing/destroying it releases held keys on worker crash. */
static int keyboard_fd=-1;
static uint8_t control_down[8];
static int control_codes[8][3]={{KEY_SPACE,0,0},{KEY_F12,0,0},{KEY_ENTER,0,0},{KEY_UP,0,0},{KEY_DOWN,0,0},{KEY_LEFT,0,0},{KEY_RIGHT,0,0},{KEY_ESC,0,0}};
static void keyboard_close(void) {
  if(keyboard_fd>=0){ioctl(keyboard_fd,UI_DEV_DESTROY);close(keyboard_fd);keyboard_fd=-1;}
  memset(control_down,0,sizeof control_down);
}
static int keyboard_open(void) {
  if(keyboard_fd>=0)return 0;
  int fd=open("/dev/uinput",O_WRONLY|O_NONBLOCK|O_CLOEXEC);
  if(fd<0)return -1;
  struct uinput_user_dev dev;memset(&dev,0,sizeof dev);
  strcpy(dev.name,"Button Goblin Mapping Keyboard");dev.id.bustype=BUS_USB;dev.id.vendor=0x1209;dev.id.product=0x5344;dev.id.version=1;
  if(ioctl(fd,UI_SET_EVBIT,EV_KEY)<0||ioctl(fd,UI_SET_EVBIT,EV_SYN)<0)goto fail;
  for(int i=0;i<8;i++)for(int j=0;j<3&&control_codes[i][j];j++)if(ioctl(fd,UI_SET_KEYBIT,control_codes[i][j])<0)goto fail;
  if(write(fd,&dev,sizeof dev)!=(ssize_t)sizeof dev||ioctl(fd,UI_DEV_CREATE)<0)goto fail;
  keyboard_fd=fd;return 0;
fail:close(fd);return -1;
}
static int control_change(int action,int down) {
  int index=action-SPACE_BUTTON;if(index<0||index>=8)return -1;
  if(control_down[index]==down)return 0;
  if(keyboard_fd<0&&(!down||keyboard_open()<0))return -1;
  struct input_event events[4];memset(events,0,sizeof events);int count=0;
  for(int j=0;j<3&&control_codes[index][j];j++){
    int code=control_codes[index][j];events[count].type=EV_KEY;events[count].code=code;events[count++].value=down;
  }
  events[count++].type=EV_SYN;
  size_t bytes=(size_t)count*sizeof events[0];ssize_t sent;
  do{sent=write(keyboard_fd,events,bytes);}while(sent<0&&errno==EINTR);
  if(sent!=(ssize_t)bytes){keyboard_close();return -1;}
  control_down[index]=(uint8_t)down;return 0;
}
static void controls_release(void){for(int a=SPACE_BUTTON;a<=ESC_BUTTON;a++)if(control_change(a,0)<0)break;}
static void controls_prepare(void){for(int k=0;k<KEYS;k++)if(cfg->keys[k].action>=SPACE_BUTTON&&cfg->keys[k].action<=ESC_BUTTON){if(keyboard_open()<0)logmsg("Mapping keyboard unavailable: %s",strerror(errno));break;}}

static void reset_usb_state(void) {
  controls_release();
  upload_wait_since = 0;
  active_upload_key = -1;
  next_upload_key = 0;
  memset(&inputs, 0, sizeof inputs);
  memset(inputs.armed, 1, sizeof inputs.armed);
  for (int k = 0; k < KEYS; k++) {
    uploads[k].wanted = 0;
    uploads[k].current = -1;
    uploads[k].finished = 0;
    uploads[k].offset = 0;
    uploads[k].chunk = 0;
    uploads[k].frame = 0;
    uploads[k].frame_due = 0;
    restore[k] = 0;
  }
  if (!splash_active()) restore_script_visual();
}
/* Never reinterpret a key already down as a fresh press after reload. */
static void reset_after_reload(void) {
  uint8_t held[KEYS];
  memcpy(held, inputs.down, sizeof held);
  reset_usb_state();
  for (int k = 0; k < KEYS; k++) {
    inputs.down[k] = held[k];
    inputs.armed[k] = !held[k];
  }
}
/* One output report per turn, interleaved with draining input. Complete the
 * current JPEG before switching keys/states: the deck shares its assembly
 * buffer. No SD access here. */
/* Finished feedback pulses once per minute; its final gray frame rests
 * for the remainder. This timing is separate from the short SDAN loop. */
static double frame_seconds(int key, int state, struct image *im, unsigned frame) {
  if (state == 3 && cfg->keys[key].action == UPDATE_ALL &&
      im->frame_count > 1 && frame == im->frame_count - 1) {
    double pulse = 0;
    for (unsigned i = 0; i + 1 < im->frame_count; i++)
      pulse += im->frames[i].duration_ms / 1000.0;
    return pulse < 60 ? 60 - pulse : 0.2;
  }
  return im->frames[frame].duration_ms / 1000.0;
}
static int upload_step(int fd) {
  /* Give press/release feedback priority over the next animation frame.
     An in-flight JPEG must still finish before the shared buffer is reused. */
  int priority=-1;
  if(active_upload_key<0)for(int scan=0;scan<KEYS;scan++){
    int k=(next_upload_key+scan)%KEYS;
    if(uploads[k].current!=uploads[k].wanted){priority=k;break;}
  }
  for (int scan = 0; scan < KEYS; scan++) {
    int k = active_upload_key >= 0 ? active_upload_key
              : priority >= 0 ? priority
                                   : (next_upload_key + scan) % KEYS;
    struct upload *u = &uploads[k];
    if (u->current == u->wanted && u->finished)
      continue;
    if (u->current != u->wanted && u->offset == 0) {
      u->current = u->wanted;
      u->finished = 0;
      u->offset = 0;
      u->chunk = 0;
      u->frame = 0;
      u->frame_due = 0;
    }
    active_upload_key = k;
    struct image *im = &(splash_active() ? startup_cfg : cfg)->keys[k].images[u->current];
    const uint8_t *data =
        im->frame_count ? im->frames[u->frame].data : im->data;
    size_t size = im->frame_count ? im->frames[u->frame].size : im->size;
    size_t n = size - u->offset;
    if (n > 1016)
      n = 1016;
    uint8_t b[1024];
    packet(b, k, u->chunk, data + u->offset, n, u->offset + n == size);
    ssize_t written = write(fd, b, sizeof b);
    if (written < 0 && (errno == EAGAIN || errno == EINTR)) {
      double now = nowtime();
      if (!upload_wait_since)
        upload_wait_since = now;
      if (now - upload_wait_since >= 2) {
        logmsg("USB output made no progress for 2 seconds; reconnecting");
        upload_wait_since = 0;
        return -1;
      }
      return 0;
    }
    if (written != 1024)
      return -1;
    upload_wait_since = 0;
    u->offset += n;
    u->chunk++;
    if (u->offset == size) {
      u->finished = 1;
      if (im->frame_count > 1)
        u->frame_due = nowtime() + frame_seconds(k, u->current, im, u->frame);
      next_upload_key = (k + 1) % KEYS;
      active_upload_key = -1;
      u->offset = 0;
      u->chunk = 0;
    }
    return 1;
  }
  return 0;
}
static int select_action(const struct config *, const struct queue *, double,
                         double);
static pid_t script_pid = -1;
static int script_update_lock = -1;
static int script_key = -1, script_action = NONE, script_page = -1;
static char script_target[PATHCAP];
static int completed_key = -1, completed_page = -1, completed_state = 3;
static char completed_target[PATHCAP];
/* A reload may reassign a button while its previously started job continues. */
static int script_matches_page(void) {
  const struct config *owner = script_page >= 0 && script_page < PAGES
                                  ? page_configs[script_page] : NULL;
  return script_pid > 0 && owner && script_key >= 0 && script_key < KEYS &&
         owner->keys[script_key].action == script_action &&
         !strcmp(owner->keys[script_key].target, script_target);
}
static int script_matches_config(void) {
  return active_page == script_page && script_matches_page();
}
static void restore_script_visual(void) {
  if (cfg && active_page == completed_page && completed_key >= 0 && cfg->keys[completed_key].action == UPDATE_ALL &&
      !strcmp(cfg->keys[completed_key].target, completed_target)) {
    visual(completed_key, completed_state, nowtime());
    restore[completed_key] = 0;
  }
  if (script_matches_config()) {
    visual(script_key, 1, nowtime());
    restore[script_key] = 0;
  }
}
static int script_timeout_seconds = SCRIPT_TIMEOUT_SECONDS;
static double script_started, next_script_poll;
static void advance_animations(double now) {
  if (page_transition) return; /* Complete the initial paint before animating. */
  if (startup_stage && startup_stage != 5) return;
  for (int k = 0; k < KEYS; k++) {
    struct upload *u = &uploads[k];
    if (u->current != u->wanted || u->current < 0 || !u->finished)
      continue;
    struct image *im = &(splash_active() ? startup_cfg : cfg)->keys[k].images[u->current];
    if (im->frame_count > 1 && u->frame_due > 0 && now >= u->frame_due) {
      u->frame = (u->frame + 1) % im->frame_count;
      u->offset = 0;
      u->chunk = 0;
      u->finished = 0;
      u->frame_due = 0;
    }
  }
}
static int upload_pending(void) {
  for (int k = 0; k < KEYS; k++)
    if (uploads[k].current != uploads[k].wanted || !uploads[k].finished)
      return 1;
  return 0;
}
static int navigation_hold_key(int k) {
  return active_page == 0 && k == BRIGHTNESS_KEY && page_configs[NAV_PAGE] &&
         cfg->keys[k].action == CYCLE;
}
static int pending_hold(int k) {
  return inputs.armed[k] && inputs.down[k] && !inputs.held[k];
}
static int configured_hold(int k) {
  return !navigation_hold_key(k) && cfg->keys[k].action < SPACE_BUTTON &&
         cfg->keys[k].trigger == HOLD;
}
/* poll wakes immediately on HID input; idle housekeeping wakes once/second.
   Every timed action contributes its own deadline, so longer idle waits do
   not delay holds, cooldowns, script timeouts or icon restoration. */
static int worker_wait_ms(double now, int connected, double next_connect,
                          double heartbeat, double status, double next_upload,
                          double last_launch) {
  double deadline = now + 1;
  if (startup_stage && connected)
    earlier(&deadline, startup_stage == 2 ? startup_since + 1.2 : now + 0.04);
  earlier(&deadline, heartbeat);
  earlier(&deadline, status);
  if (!connected)
    earlier(&deadline, next_connect);
  else {
    if (upload_pending())
      earlier(&deadline, next_upload);
    if (queue.count) {
      if (select_action(cfg, &queue, now, last_launch) >= 0)
        return 0;
      earlier(&deadline, last_launch + cfg->gap_ms / 1000.0);
    }
    for (int k = 0; k < KEYS; k++) {
      if (!page_transition && (!startup_stage || startup_stage == 5) && uploads[k].finished && uploads[k].current == uploads[k].wanted &&
          uploads[k].frame_due > 0)
        earlier(&deadline, uploads[k].frame_due);
      if (restore[k])
        earlier(&deadline, restore[k]);
      if (!startup_stage && !page_transition && pending_hold(k)) {
        if (navigation_hold_key(k)) earlier(&deadline,inputs.since[k]+1.0);
        else if (configured_hold(k))
          earlier(&deadline,inputs.since[k]+cfg->keys[k].hold_ms/1000.0);
      }
    }
  }
  if (script_pid > 0)
    earlier(&deadline,next_script_poll>now?next_script_poll:now+0.1);
  if (script_pid > 0)
    earlier(&deadline, script_started + script_timeout_seconds > now
                           ? script_started + script_timeout_seconds
                           : now + 0.1);
  return timeout_ms(now, deadline);
}
static int decode(const uint8_t *b, size_t n, uint8_t states[KEYS]) {
  if (n < 19 || b[0] != 1 || b[1] != 0 || b[2] != 15 || b[3] != 0)
    return -1;
  for (int k = 0; k < KEYS; k++)
    if (b[k + 4] > 1)
      return -1;
  memcpy(states, b + 4, KEYS);
  return 0;
}
static int enqueue(struct queue *q, int k) {
  if (q->count >= QCAP)
    return -1;
  q->keys[(q->head + q->count) % QCAP] = k;
  q->count++;
  return 0;
}
static int dequeue_at(struct queue *q, int index) {
  if (index < 0 || index >= q->count)
    return -1;
  int key = q->keys[(q->head + index) % QCAP];
  if (index == 0) {
    q->head = (q->head + 1) % QCAP;
    q->count--;
    return key;
  }
  for (int i = index; i < q->count - 1; i++)
    q->keys[(q->head + i) % QCAP] = q->keys[(q->head + i + 1) % QCAP];
  q->count--;
  return key;
}

static int select_action(const struct config *c, const struct queue *q,
                         double now, double last_launch) {
  for (int i = 0; i < q->count; i++) {
    int key = q->keys[(q->head + i) % QCAP];
    if (c->keys[key].action != LAUNCH ||
        now - last_launch >= c->gap_ms / 1000.0)
      return i;
  }
  return -1;
}
static void fire(int k, double now) {
  input_count++;
  if (cfg->keys[k].action == NONE)
    return;
  if (enqueue(&queue, k)) {
    queue_overflow++;
    visual(k, 2, now);
    logmsg("Queue full: key %d rejected explicitly", k);
  }
}
/* Every changed key is handled. A reconnect suppresses each initially-held key
   until its release; this avoids replaying a press after USB/worker recovery.
 */
static void handle_input(const uint8_t states[KEYS], double now,
                         void (*emit)(int, double)) {
  for (int k = 0; k < KEYS; k++) {
    if (!inputs.armed[k]) {
      if (!states[k])
        inputs.armed[k] = 1;
      inputs.down[k] = states[k];
      continue;
    }
    int pressed = states[k] && !inputs.down[k];
    int released = !states[k] && inputs.down[k];
    int action = cfg->keys[k].action;
    if (navigation_hold_key(k)) {
      if (pressed) { inputs.since[k]=now; inputs.held[k]=0; }
      if (released && !inputs.held[k]) {
        if (now-inputs.since[k]>=1.0) {
          memcpy(inputs.down,states,sizeof inputs.down);
          switch_page(NAV_PAGE);
          return;
        }
        emit(k,now);
      }
      inputs.down[k]=states[k];
      continue;
    }
    if (action==BACK_PAGE) {
      if (pressed || released) { visual(k,states[k]?1:0,now); restore[k]=0; }
      if (released) {
        memcpy(inputs.down,states,sizeof inputs.down);
        switch_page(0);
        return;
      }
      inputs.down[k]=states[k];
      continue;
    }
    if (action>=SPACE_BUTTON && action<=ESC_BUTTON) {
      if (pressed || released) {
        /* Multiple physical buttons may map to the same virtual key.
           Use this complete report, not the previously processed key order. */
        int down=0;
        for(int j=0;j<KEYS;j++)
          if(inputs.armed[j]&&states[j]&&cfg->keys[j].action==action)down=1;
        int rc=control_change(action,down);
        visual(k,rc?2:(states[k]?1:0),now);
        if (!rc) restore[k]=0;
      }
      inputs.down[k]=states[k];
      continue;
    }
    if (pressed) {
      inputs.since[k] = now;
      inputs.held[k] = 0;
      if (cfg->keys[k].trigger == PRESS)
        emit(k, now);
    }
    if (released) {
      if (cfg->keys[k].trigger == RELEASE)
        emit(k, now);
      if (cfg->keys[k].trigger == HOLD && !inputs.held[k] &&
          now - inputs.since[k] >= cfg->keys[k].hold_ms / 1000.0)
        emit(k, now);
      inputs.held[k] = 0;
    }
    inputs.down[k] = states[k];
  }
}
/* Track presses during splash/page painting without firing them afterward. */
static void handle_device_input(const uint8_t states[KEYS], double now,
                                void (*emit)(int,double)) {
  if(!startup_stage&&page_transition&&!upload_pending())page_transition=0;
  if(startup_stage||page_transition) {
    for(int k=0;k<KEYS;k++) {
      inputs.down[k]=states[k];inputs.armed[k]=!states[k];inputs.held[k]=0;
    }
  } else handle_input(states,now,emit);
}
static int prime_inputs(int fd) {
  for (int count = 0; count < 64; count++) {
    uint8_t b[512], states[KEYS];
    ssize_t n = read(fd, b, sizeof b);
    if (n < 0 && errno == EINTR) {
      count--;
      continue;
    }
    if (n < 0 && errno == EAGAIN)
      return 0;
    if (n <= 0)
      return -1;
    if (decode(b, (size_t)n, states))
      continue;
    for (int k = 0; k < KEYS; k++) {
      inputs.down[k] = states[k];
      inputs.armed[k] = !states[k];
      inputs.held[k] = 0;
    }
  }
  return 0;
}
static void handle_holds(double now, void (*emit)(int, double)) {
  if (navigation_hold_key(BRIGHTNESS_KEY) && pending_hold(BRIGHTNESS_KEY) &&
      now-inputs.since[BRIGHTNESS_KEY]>=1.0) {
    inputs.held[BRIGHTNESS_KEY]=1;
    switch_page(NAV_PAGE);
    return;
  }
  for (int k = 0; k < KEYS; k++)
    if (configured_hold(k) && pending_hold(k) &&
        now - inputs.since[k] >= cfg->keys[k].hold_ms / 1000.0) {
      inputs.held[k] = 1;
      emit(k, now);
    }
}
static int command_write(const char *interface, const char *target) {
  char cmd[1100];
  if (strlen(target) > 1000 || !cleanpath(target) || !valid_target(target))
    return -1;
  struct stat st;
  if (stat(interface, &st) || (!S_ISCHR(st.st_mode) && !S_ISFIFO(st.st_mode)))
    return -1;
  int fd = open(interface, O_WRONLY | O_NONBLOCK);
  if (fd < 0)
    return -1;
  int len = snprintf(cmd, sizeof cmd, "load_core %s\n", target);
  ssize_t n;
  do {
    n = write(fd, cmd, (size_t)len);
  } while (n < 0 && errno == EINTR && running);
  close(fd);
  return n == len ? 0 : -1;
}
static int launch(int key) {
  char p[PATHCAP];
  if (resolve(cfg, key, p)) {
    logmsg("Missing/invalid target: key %d %s", key, cfg->keys[key].label);
    return -1;
  }
  if (command_write("/dev/MiSTer_cmd", p)) {
    logmsg("Launch write failed: %s", strerror(errno));
    return -1;
  }
  launch_count++;
  logmsg("Launch request sent: %s", p);
  return 0;
}
/* Shell scripts run in their own process group. Never interpolate a path into
   shell code. One script at a time, with a 60-second deadline. */
static int start_script(int key, int usb_fd, int lock_fd) {
  char path[PATHCAP], logpath[PATHCAP];
  if (script_pid > 0) {
    logmsg("Script already running: key %d (PID %ld); ignored key %d",
           script_key, (long)script_pid, key);
    return -1;
  }
  if (resolve(cfg, key, path)) {
    logmsg("Script target missing/invalid: key %d: %s", key,
           cfg->keys[key].target);
    return -1;
  }
  if (runtimepath(logpath, ".script.log")) {
    logmsg("Script log path invalid: key %d", key);
    return -1;
  }
  /* Shared by the Mini and MK.2; never run two Update All jobs. */
  int updater_lock = -1;
  if (cfg->keys[key].action == UPDATE_ALL) {
    updater_lock = open("/tmp/buttongoblin_update_all.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (updater_lock < 0) return -1;
    if (flock(updater_lock, LOCK_EX | LOCK_NB)) {
      close(updater_lock);
      logmsg("Update All is already running on another deck; request ignored");
      return -2;
    }

  }
  int out = open(logpath, O_CREAT | O_WRONLY | O_TRUNC, 0600);
  if (out < 0) {
    if (updater_lock >= 0) close(updater_lock);
    logmsg("Cannot open script log: key %d: %s", key, strerror(errno));
    return -1;
  }
  pid_t parent = getpid(), pid = fork();
  if (pid == 0) {
    if (setpgid(0, 0) || prctl(PR_SET_PDEATHSIG, SIGTERM) ||
        getppid() != parent)
      _exit(126);
    running = 1;
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_DFL);
    int capture[2];
    if (pipe(capture))
      _exit(126);
    if (fcntl(capture[0], F_SETFL, O_NONBLOCK) < 0)
      _exit(126);
    int in = open("/dev/null", O_RDONLY);
    if (in < 0 || dup2(in, 0) < 0 || dup2(capture[1], 1) < 0 ||
        dup2(capture[1], 2) < 0)
      _exit(126);
    if (in > 2)
      close(in);

    if (usb_fd >= 0)
      close(usb_fd);
    if (heartbeat_fd >= 0)
      close(heartbeat_fd);
    close(lock_fd);
    if (chdir(base))
      _exit(126);
    /* Guardian survives exec and kills the whole group if its worker dies. */
    sigset_t blocked, original;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGCHLD);
    sigaddset(&blocked, SIGTERM);
    sigaddset(&blocked, SIGINT);
    signal(SIGCHLD, child_signal);
    if (sigprocmask(SIG_BLOCK, &blocked, &original))
      _exit(126);
    pid_t guardian = getpid(), shell = fork();
    if (shell < 0)
      _exit(126);
    if (shell == 0) {
      if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != guardian)
        _exit(126);
      if (sigprocmask(SIG_SETMASK, &original, NULL))
        _exit(126);
      close(capture[0]);
      close(capture[1]);
      close(out);
      signal(SIGCHLD, SIG_DFL);
      signal(SIGTERM, SIG_DFL);
      signal(SIGINT, SIG_DFL);
      signal(SIGHUP, SIG_DFL);
      if (cfg->keys[key].action == UPDATE_ALL) {
        /* Match a direct SSH launch, with the script's own working directory.
         */
        char directory[PATHCAP];
        strcpy(directory, path);
        char *slash = strrchr(directory, '/');
        if (!slash)
          _exit(126);
        if (slash == directory)
          slash[1] = 0;
        else
          *slash = 0;
        if (chdir(directory) || setenv("HOME", "/root", 1) ||
            setenv("TERM", "linux", 1) || setenv("LC_ALL", "en_US.UTF-8", 1) ||
            setenv("UPDATE_ALL_NON_INTERACTIVE", "true", 1))
          _exit(126);
        execl("/bin/bash", "bash", path, (char *)NULL);
      } else
        execl("/bin/sh", "sh", path, (char *)NULL);
      _exit(127);
    }
    close(capture[1]);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    size_t logged = 0;
    for (;;) {
      if (!running || getppid() != parent) {
        kill(-guardian, SIGKILL);
        _exit(126);
      }
      int status;
      pid_t done = waitpid(shell, &status, WNOHANG);
      int wait_error = errno;
      /* Drain even after exit. Keep recent output in a bounded file: truncating
         the same inode lets an existing tail -f continue after the cap. */
      int eof = 0;
      for (int i = 0; i < 32; i++) {
        uint8_t buffer[8192];
        ssize_t n = read(capture[0], buffer, sizeof buffer);
        if (n == 0) {
          eof = 1;
          break;
        }
        if (n < 0) {
          if (errno == EINTR)
            continue;
          break;
        }
        size_t keep = (size_t)n;
        if (keep > SCRIPT_LOG_CAP - logged) {
          if (ftruncate(out, 0) == 0 && lseek(out, 0, SEEK_SET) >= 0)
            logged = 0;
          else
            keep = SCRIPT_LOG_CAP - logged;
        }
        size_t offset = 0;
        while (offset < keep) {
          ssize_t written = write(out, buffer + offset, keep - offset);
          if (written < 0 && errno == EINTR)
            continue;
          if (written <= 0) {
            logged = SCRIPT_LOG_CAP;
            break;
          }
          offset += (size_t)written;
        }
        if (logged < SCRIPT_LOG_CAP)
          logged += offset;
      }
      if (done == shell)
        _exit(WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
      if (done < 0 && wait_error != EINTR)
        _exit(126);
      /* SIGCHLD/TERM are atomically unblocked during the wait: no lost signal.
         A script can close stdout while continuing, so do not spin on EOF. */
      struct pollfd watch = {capture[0], POLLIN, 0};
      ppoll(eof ? NULL : &watch, eof ? 0 : 1, NULL, &original);
    }
  }
  close(out);
  if (pid < 0) {
    if (updater_lock >= 0) close(updater_lock);
    logmsg("Script fork failed: %s", strerror(errno));
    return -1;
  }
  script_update_lock = updater_lock;
  setpgid(pid, pid);
  if (completed_page == active_page && completed_key == key) completed_key = -1;
  script_pid = pid;
  script_key = key;
  script_page = active_page;
  script_action = cfg->keys[key].action;
  strcpy(script_target, cfg->keys[key].target);
  script_started = nowtime();
  next_script_poll = script_started;
  child_changed = 0;
  /* Update All performs network downloads; ordinary scripts remain bounded. */
  script_timeout_seconds =
      cfg->keys[key].action == UPDATE_ALL ? 3600 : SCRIPT_TIMEOUT_SECONDS;
  logmsg("Script started: %s", path);
  return 0;
}
static void reap_script(double now, int stopping) {
  if (script_pid <= 0)
    return;
  int status;
  pid_t result;
  do {
    result = waitpid(script_pid, &status, WNOHANG);
  } while (result < 0 && errno == EINTR);
  int expired = now - script_started >= script_timeout_seconds;
  if (result == 0 && !stopping && !expired)
    return;
  /* Also stop descendants when the owning shell exits. */
  kill(-script_pid, SIGKILL);
  if (result == 0) {
    result = waitpid(script_pid, &status, WNOHANG);
    if (result == 0)
      return;
  }
  int success = result > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
                !expired && !stopping;
  logmsg("Script %s: key %d",
         success   ? "completed"
         : expired ? "timed out"
                   : "failed/stopped",
         script_key);
  if (result > 0 && WIFEXITED(status))
    logmsg("Script exit code: key %d = %d", script_key, WEXITSTATUS(status));
  if (script_matches_page()) {
    struct upload *owner = active_page == script_page ? uploads : page_uploads[script_page];
    double *times = active_page == script_page ? restore : page_restore[script_page];
    int state = success ? (script_action == UPDATE_ALL ? 3 : 1) : 2;
    owner[script_key].wanted = state;
    times[script_key] = script_action == UPDATE_ALL ? 0 : now + 2;
    if (script_action == UPDATE_ALL) {
      completed_key = script_key;
      completed_page = script_page;
      completed_state = success ? 3 : 2;
      strcpy(completed_target, script_target);
    }
  }
  if (script_update_lock >= 0) { close(script_update_lock); script_update_lock = -1; }
  script_pid = -1;
  script_key = -1;
  script_page = -1;
  script_action = NONE;
  script_target[0] = 0;
}
static struct config *load_shortcuts(void){
  char path[PATHCAP];if(textset(path,sizeof path,configpath))return NULL;
  char *slash=strrchr(path,'/');if(slash)slash[1]=0;else path[0]=0;
  if(strlen(path)+strlen("buttons-shortcuts.conf")>=sizeof path)return NULL;
  strcat(path,"buttons-shortcuts.conf");if(!regular(path))return NULL;
  struct config *c=newconfig();if(!c)return NULL;
  if(parseconfig(c,path)||loadimages(c)){freeconfig(c);return NULL;}return c;
}
static void switch_page(int page){
  if(page<0||page>=PAGES||!page_configs[page]||page==active_page)return;
  uint8_t held[KEYS];memcpy(held,inputs.down,sizeof held);
  memcpy(page_uploads[active_page],uploads,sizeof uploads);memcpy(page_restore[active_page],restore,sizeof restore);
  /* Held keyboard/navigation feedback belongs only to the visible page. */
  for(int k=0;k<KEYS;k++)if(cfg->keys[k].action>=SPACE_BUTTON){page_uploads[active_page][k].wanted=0;page_restore[active_page][k]=0;}
  memset(&queue,0,sizeof queue);active_page=page;page_transition=1;cfg=page_configs[page];reset_usb_state();controls_prepare();
  for(int k=0;k<KEYS;k++){uploads[k].wanted=page_uploads[page][k].wanted;restore[k]=page_restore[page][k];inputs.down[k]=held[k];inputs.armed[k]=!held[k];}
  restore_script_visual();
  logmsg("Button Goblin page %d",page+1);
}
/* Deliver script feedback to its owning page, including custom mappings. */
static void reap_page_script(double now,int stopping){
  /* SIGCHLD normally wakes us immediately; a 100 ms fallback covers races.
     Do not copy page state or call waitpid once per USB image packet. */
  if(script_pid<=0)return;
  if(!stopping&&!child_changed&&now<next_script_poll&&now-script_started<script_timeout_seconds)return;
  child_changed=0;next_script_poll=now+0.1;
  reap_script(now,stopping);
}

static void worker_status(int connected) {
  char path[PATHCAP], tmp[PATHCAP];
  if (runtimepath(path, ".worker_status") ||
      runtimepath(tmp, ".worker_status.tmp"))
    return;
  FILE *f = fopen(tmp, "w");
  if (!f)
    return;
  fprintf(f,
          "version=%s\nworker_pid=%ld\nusb_connected=%d\ninput_triggers=%"
          "lu\nqueued=%d\nqueue_overflow=%lu\nlaunch_requests=%lu\nusb_"
          "connections=%lu\ninvalid_reports=%lu\n",
          VERSION, (long)getpid(), connected, input_count, queue.count,
          queue_overflow, launch_count, usb_reconnects, invalid_reports);
  fclose(f);
  rename(tmp, path);
}
/* Centralize USB teardown only; callers decide whether to clear queued actions.
 * Keeping those decisions explicit preserves reconnect/reload semantics. */
static void disconnect_usb(int *fd) {
  controls_release();
  if (*fd >= 0)
    close(*fd);
  *fd = -1;
}
/* Preload the short startup mosaic; runtime uploads never decode images. */
static struct config *load_startup(void) {
  struct config *c = newconfig();
  if (!c) return NULL;
  for (int k = 0; k < KEYS; k++) {
    c->keys[k].action = NONE;
    char path[32];
    snprintf(path, sizeof path, "startup/%02d.jpg", k);
    for (int state = 0; state < 3; state++)
      if (pathset(&c->keys[k].paths[state], path)) { freeconfig(c); return NULL; }
  }
  if (loadimages(c)) { freeconfig(c); return NULL; }
  return c;
}
/* Fade the static mosaic in and out before starting normal button animations. */
static int startup_step(int fd, int brightness, double now) {
  if (!startup_stage) return 0;
  int level = startup_level;
  if (startup_stage == 1 && !upload_pending()) {
    startup_stage = 6; startup_since = now; level = 0;
  } else if (startup_stage == 6) {
    double t = (now - startup_since) / 2.2;
    double eased = t * t * (3 - 2 * t);
    level = t >= 1 ? brightness : (int)(brightness * eased);
    if (t >= 1) { startup_stage = 2; startup_since = now; }
  } else if (startup_stage == 2 && now - startup_since >= 1.2) {
    startup_stage = 3; startup_since = now;
  } else if (startup_stage == 3) {
    double t = (now - startup_since) / 1.3;
    double eased = t * t * (3 - 2 * t);
    level = t >= 1 ? 0 : (int)(brightness * (1 - eased));
    if (t >= 1) {
      startup_stage = 4;
      reset_after_reload(); /* Preserve held keys through the fade. */
      freeconfig(startup_cfg); startup_cfg = NULL;
    }
  } else if (startup_stage == 4 && !upload_pending()) {
    startup_stage = 5; startup_since = now;
  } else if (startup_stage == 5) {
    double t = (now - startup_since) / 3.0;
    double eased = t * t * (3 - 2 * t);
    level = t >= 1 ? brightness : (int)(brightness * eased);
    if (t >= 1) {
      startup_stage = 0;
      if (prime_inputs(fd)) return -1;
      logmsg("Startup MiSTer mosaic/fade completed");
    }
  }
  if (level != startup_level) {
    if (feature(fd, 8, level) < 0) return -1;
    startup_level = level;
  }
  return 0;
}
/* A HUP during the splash must keep the deck dark while tiles reload. */
static int reload_brightness(int fd, int brightness) {
  if (splash_active()) {
    startup_stage = 1; startup_level = 0;
  }
  return fd >= 0 ? feature(fd, 8, startup_stage ? startup_level : brightness) : 0;
}
static int worker(void) {
  char worker_lock_path[PATHCAP];
  if (runtimepath(worker_lock_path, ".worker_lock"))
    return 2;
  int worker_lock = open(worker_lock_path, O_CREAT | O_RDWR, 0600);
  if (worker_lock < 0)
    return 2;
  if (flock(worker_lock, LOCK_EX | LOCK_NB)) {
    logmsg("Worker lock busy; refusing a second USB owner");
    close(worker_lock);
    return 3;
  }
  reloading = 0;
  signal(SIGCHLD, child_signal);
  signal(SIGTERM, stop);
  signal(SIGINT, stop);
  signal(SIGHUP, reload_signal);
  signal(SIGPIPE, SIG_IGN);
  cfg = readconfig(1);
  if (!cfg) {
    logmsg("Configuration/image validation failed; fix buttons.conf or images");
    close(worker_lock);
    return 2;
  }
  page_configs[0]=cfg;page_configs[1]=load_shortcuts();active_page=0;
  memset(page_uploads,0,sizeof page_uploads);memset(page_restore,0,sizeof page_restore);
  if(page_configs[1]&&keyboard_open()<0)logmsg("Shortcut keyboard unavailable: %s",strerror(errno));
  controls_prepare();
  startup_cfg = load_startup();
  logmsg(startup_cfg ? "Startup MiSTer tiles cached" : "Startup splash unavailable: copy icons/startup with all 15 tiles");
  startup_stage = startup_shown = 0;
  memset(&queue, 0, sizeof queue);
  reset_usb_state();
  input_count = queue_overflow = launch_count = usb_reconnects =
      invalid_reports = 0;
  int fd = -1, brightness = cfg->brightness, waiting = 0;
  double next_connect = 0, last_launch = -100, next_heartbeat = 0,
         next_status = 0, next_upload = 0;
  logmsg("Worker ready; image cache loaded");
  while (running) {
    double now = nowtime();
    reap_page_script(now, 0);
    if (now >= next_heartbeat) {
      if (heartbeat_fd >= 0) {
        uint8_t b = 1;
        ssize_t sent = write(heartbeat_fd, &b, 1);
        if (sent < 0 && errno != EAGAIN && errno != EINTR) {
          running = 0;
          continue;
        }
      }
      next_heartbeat = now + 1;
    }
    if (now >= next_status) {
      worker_status(fd >= 0);
      next_status = now + STATUS_INTERVAL;
    }
    if (reloading) {
      reloading = 0;
      struct config *updated = readconfig(1);
      struct config *shortcuts=page_configs[1]?load_shortcuts():NULL;
      if(updated&&page_configs[1]&&!shortcuts){freeconfig(updated);updated=NULL;}
      if (updated) {
        controls_release();
        if(page_configs[0]){freeconfig(page_configs[0]);freeconfig(page_configs[1]);}else freeconfig(cfg);
        page_configs[0]=updated;page_configs[1]=shortcuts;active_page=0;
        memset(page_uploads,0,sizeof page_uploads);memset(page_restore,0,sizeof page_restore);
        cfg = updated;
        brightness = cfg->brightness;
        memset(&queue, 0, sizeof queue);
        reset_after_reload();
        if (reload_brightness(fd, brightness) < 0) {
          disconnect_usb(&fd);
        }
        logmsg("Configuration reloaded atomically; old queued actions cleared");
      } else {freeconfig(shortcuts);
        logmsg("Reload rejected; prior working configuration retained");}
    }
    if (fd < 0 && now >= next_connect) {
      char path[128];
      fd = finddeck(path);
      next_connect = now + 2;
      if (fd < 0) {
        if (!waiting) {
          logmsg("Waiting for MK.2 HIDRAW interface");
          waiting = 1;
        }
      } else {
        waiting = 0;
        usb_reconnects++;
        if (startup_cfg && (!startup_shown || startup_stage)) {
          startup_stage = 1; startup_shown = 1; startup_level = 0;
          logmsg("Startup MiSTer splash beginning; covering built-in logo");
        } else if (startup_stage) {
          startup_stage = 0; freeconfig(startup_cfg); startup_cfg = NULL;
        }
        logmsg("USB connected: %s", path);
        memset(&queue, 0, sizeof queue);
        reset_usb_state();
        if (feature(fd, 8, startup_stage ? 0 : brightness) < 0 || prime_inputs(fd)) {
          logmsg("USB initialize failed");
          disconnect_usb(&fd);
        }
      }
    }
    if (fd < 0) {
      poll(NULL, 0,
           worker_wait_ms(nowtime(), 0, next_connect, next_heartbeat,
                          next_status, next_upload, last_launch));
      continue;
    }
    /* Drain every report, including press+release bursts accumulated during LCD
     * writes. */
    int bad = 0;
    for (int count = 0; count < 64; count++) {
      uint8_t b[512], states[KEYS];
      ssize_t n = read(fd, b, sizeof b);
      if (n < 0 && (errno == EAGAIN || errno == EINTR))
        break;
      if (n <= 0) {
        bad = 1;
        break;
      }
      if (decode(b, (size_t)n, states)) {
        invalid_reports++;
        continue;
      }
      handle_device_input(states,nowtime(),fire);
    }
    if (bad) {
      logmsg("USB read disconnected");
      disconnect_usb(&fd);
      memset(&queue, 0, sizeof queue);
      continue;
    }
    now = nowtime();
    if(!startup_stage&&page_transition&&!upload_pending())page_transition=0;
    if (!startup_stage&&!page_transition) handle_holds(now, fire);
    for (int k = 0; k < KEYS; k++)
      if (restore[k] && now >= restore[k])
        visual(k, 0, now);
    /* Launches keep FIFO order; controls bypass a pending launch cooldown. */
    if (!startup_stage && queue.count) {
      int pick = select_action(cfg, &queue, now, last_launch);
      if (pick >= 0) {
        int k = dequeue_at(&queue, pick);
        int action = cfg->keys[k].action;
        if (action == SCRIPT || action == UPDATE_ALL) {
          if (script_pid > 0) {
            logmsg("Script already running: key %d; ignored key %d", script_key,
                   k);
            /* A second press must not label a running updater as failed. */
            if (k != script_key || !script_matches_config())
              visual(k, 2, now);
          } else {
            int rc = start_script(k, fd, worker_lock);
            visual(k, rc ? 2 : 1, now);
            if (rc && action == UPDATE_ALL) {
              completed_key=k;completed_page=active_page;completed_state=2;
              strcpy(completed_target,cfg->keys[k].target);
            }
            if (!rc || action == UPDATE_ALL)
              restore[k] =
                  0; /* Keep running/failure feedback until exit/retry. */
          }
        } else if (action == LAUNCH) {
          int rc = launch(k);
          if (!rc)
            last_launch = nowtime();
          visual(k, rc ? 2 : 1, nowtime());
        } else if (action == DIM || action == BRIGHT || action == CYCLE) {
          brightness = next_brightness(action, brightness);
          if (feature(fd, 8, brightness) < 0) {
            logmsg("Brightness command failed: key %d: %s", k, strerror(errno));
            bad = 1;
          } else {
            logmsg("Brightness set: key %d = %d%%", k, brightness);
            visual(k, 1, now);
          }
        } else if (action == RELOAD)
          reloading = 1;
        else if (action == RESTART) {
          logmsg("Restart requested by configured key %d", k);
          running = 0;
        }
      }
    }
    if (bad) {
      disconnect_usb(&fd);
      continue;
    }
    if (!running)
      break;
    /* One timestamp for this scheduling pass; refresh after USB writes. */
    now = nowtime();
    if (startup_step(fd, brightness, now) < 0) {
      disconnect_usb(&fd); continue;
    }
    advance_animations(now);
    int uploading = 0;
    if (now >= next_upload && upload_pending()) {
      uploading = upload_step(fd);
      next_upload =
          nowtime() + (uploading == 0 ? 40 : UPLOAD_INTERVAL_MS) / 1000.0;
    }
    if (uploading < 0) {
      logmsg("USB image write failed");
      disconnect_usb(&fd);
      continue;
    }
    struct pollfd p = {fd, POLLIN, 0};
    int rc = poll(&p, 1,
                  worker_wait_ms(nowtime(), 1, next_connect, next_heartbeat,
                                 next_status, next_upload, last_launch));
    if (rc < 0 && errno != EINTR)
      bad = 1;
    if (rc > 0 && (p.revents & (POLLERR | POLLHUP | POLLNVAL)))
      bad = 1;
    if (bad) {
      disconnect_usb(&fd);
    }
  }
  /* No feature ioctl on exit: a faulty USB control transfer must not block
   * stopping. */
  if (fd >= 0)
    close(fd);
  controls_release();keyboard_close();
  reap_page_script(nowtime(), 1);
  worker_status(0);
  freeconfig(startup_cfg); startup_cfg = NULL; startup_stage = 0;
  freeconfig(page_configs[0]);freeconfig(page_configs[1]);page_configs[0]=page_configs[1]=NULL;active_page=0;
  cfg = NULL;
  close(worker_lock);
  return 0;
}
static void supervisor_status(pid_t child, unsigned restarts,
                              const char *state) {
  char p[PATHCAP], tmp[PATHCAP];
  if (runtimepath(p, ".status") || runtimepath(tmp, ".status.tmp"))
    return;
  FILE *f = fopen(tmp, "w");
  if (!f)
    return;
  fprintf(
      f,
      "version=%s\nsupervisor_pid=%ld\nworker_pid=%ld\nrestarts=%u\nstate=%s\n",
      VERSION, (long)getpid(), (long)child, restarts, state);
  fclose(f);
  rename(tmp, p);
}
static void terminate_child(pid_t child, unsigned restarts) {
  if (child <= 0)
    return;
  kill(child, SIGTERM);
  double deadline = nowtime() + 2;
  while (nowtime() < deadline) {
    pid_t r = waitpid(child, NULL, WNOHANG);
    if (r == child || (r < 0 && errno == ECHILD))
      return;
    poll(NULL, 0, 20);
  }
  kill(child, SIGKILL);
  /* A kernel-stuck USB task cannot be reaped until the driver returns. Keep
     the supervisor lock and never spawn a second owner of the same deck. */
  double next_status = 0;
  for (;;) {
    pid_t r = waitpid(child, NULL, WNOHANG);
    if (r == child || (r < 0 && errno == ECHILD))
      return;
    if (nowtime() >= next_status) {
      supervisor_status(child, restarts, "waiting_for_kernel_exit");
      next_status = nowtime() + 5;
    }
    poll(NULL, 0, 1000);
  }
}
static void cap_stderr_log(void) {
  struct stat st;
  if (!fstat(STDERR_FILENO, &st) && S_ISREG(st.st_mode) && st.st_size > 32768 &&
      ftruncate(STDERR_FILENO, 0) == 0) {
    off_t position = lseek(STDERR_FILENO, 0, SEEK_SET);
    if (position < 0)
      logmsg("Cannot seek capped stderr log");
  }
}
static void resource_backoff(unsigned restarts) {
  supervisor_status(-1, restarts, "resource_backoff");
  double until = nowtime() + 1;
  while (running && nowtime() < until)
    poll(NULL, 0, timeout_ms(nowtime(), until));
}
/* Keep the lock until the old worker is reaped, including kernel-stuck tasks.
 * Spawning a replacement early could give two processes ownership of HID. */
static int supervise(int (*runworker)(void), int watchdog_ms) {
  char p[PATHCAP];
  if (runtimepath(p, ".lock"))
    return 2;
  int lock = open(p, O_CREAT | O_RDWR, 0600);
  if (lock < 0)
    return 2;
  if (flock(lock, LOCK_EX | LOCK_NB)) {
    close(lock);
    fprintf(stderr, "Supervisor already running\n");
    return 1;
  }
  if (runtimepath(p, ".pid")) {
    close(lock);
    return 2;
  }
  FILE *f = fopen(p, "w");
  if (!f) {
    close(lock);
    return 2;
  }
  fprintf(f, "%ld\n", (long)getpid());
  fclose(f);
  running = 1;
  reloading = 0;
  signal(SIGCHLD, child_signal);
  signal(SIGTERM, stop);
  signal(SIGINT, stop);
  signal(SIGHUP, reload_signal);
  signal(SIGPIPE, SIG_IGN);
  unsigned restarts = 0;
  int backoff = 1;
  pid_t child = -1;
  background_priority();
  logmsg("Supervisor v%s started (background priority)", VERSION);
  while (running) {
    cap_stderr_log();
    int pipefd[2];
    if (pipe(pipefd)) {
      logmsg("Heartbeat pipe failed; retrying");
      resource_backoff(++restarts);
      continue;
    }
    if (fcntl(pipefd[0], F_SETFL, O_NONBLOCK) < 0 ||
        fcntl(pipefd[1], F_SETFL, O_NONBLOCK) < 0) {
      close(pipefd[0]);
      close(pipefd[1]);
      logmsg("Cannot configure heartbeat pipe; retrying");
      resource_backoff(++restarts);
      continue;
    }
    pid_t parent = getpid();
    child = fork();
    if (child == 0) {
      close(lock);
      close(pipefd[0]);
      heartbeat_fd = pipefd[1];
      if (prctl(PR_SET_PDEATHSIG, SIGTERM) < 0)
        _exit(2);
      if (getppid() != parent)
        _exit(0);
      int rc = runworker();
      close(heartbeat_fd);
      _exit(rc);
    }
    close(pipefd[1]);
    if (child < 0) {
      close(pipefd[0]);
      logmsg("fork failed; retrying");
      resource_backoff(++restarts);
      continue;
    }
    double launched = nowtime(), heartbeat = launched, next_status = 0;
    supervisor_status(child, restarts, "running");
    int exited = 0;
    while (running) {
      double now = nowtime();
      if (reloading) {
        reloading = 0;
        kill(child, SIGHUP);
      }
      struct pollfd q = {pipefd[0], POLLIN, 0};
      poll(&q, 1, timeout_ms(now, heartbeat + watchdog_ms / 1000.0));
      uint8_t b[256];
      ssize_t n;
      do {
        n = read(pipefd[0], b, sizeof b);
        if (n > 0)
          heartbeat = nowtime();
      } while (n > 0);
      pid_t r = waitpid(child, NULL, WNOHANG);
      if (r == child || (r < 0 && errno == ECHILD)) {
        exited = 1;
        logmsg("Worker exited; scheduling restart");
        break;
      }
      if (q.revents & (POLLHUP | POLLERR | POLLNVAL)) {
        logmsg("Worker heartbeat pipe closed; terminating for restart");
        terminate_child(child, restarts);
        exited = 1;
        break;
      }
      if (nowtime() - heartbeat >= watchdog_ms / 1000.0) {
        logmsg("Worker heartbeat stalled; terminating for restart");
        terminate_child(child, restarts);
        exited = 1;
        break;
      }
      if (now >= next_status) {
        supervisor_status(child, restarts, "running");
        next_status = now + STATUS_INTERVAL;
      }
    }
    close(pipefd[0]);
    if (!exited)
      terminate_child(child, restarts);
    child = -1;
    if (!running)
      break;
    restarts++;
    if (nowtime() - launched > 60)
      backoff = 1;
    supervisor_status(-1, restarts, "backoff");
    double until = nowtime() + backoff;
    while (running && nowtime() < until)
      poll(NULL, 0, timeout_ms(nowtime(), until));
    if (backoff < 30)
      backoff *= 2;
    if (backoff > 30)
      backoff = 30;
  }
  supervisor_status(-1, restarts, "stopped");
  runtimepath(p, ".pid");
  unlink(p);
  close(lock);
  return 0;
}
static int selftest(void) {
  uint8_t b[1024], data[1016], states[15];
  for (int i = 0; i < 1016; i++)
    data[i] = (uint8_t)i;
  packet(b, 14, 258, data, 1016, 0);
  if (b[0] != 2 || b[1] != 7 || b[2] != 14 || b[3] || b[4] != 248 ||
      b[5] != 3 || b[6] != 2 || b[7] != 1 || memcmp(b + 8, data, 1016))
    return 1;
  packet(b, 0, 0, data, 3, 1);
  if (b[3] != 1 || b[11] != 0 || b[1023] != 0)
    return 1;
  memset(b, 0, sizeof b);
  b[0] = 1;
  b[2] = 15;
  b[4] = 1;
  b[18] = 1;
  if (decode(b, 19, states) || states[0] != 1 || states[14] != 1 ||
      decode(b, 18, states) != -1)
    return 1;
  b[5] = 2;
  if (decode(b, 19, states) != -1)
    return 1;
  puts("PASS: HID input validation and output packet format");
  return 0;
}
int main(int argc, char **argv) {
  int validate = 0, resolvekey = -1, dry = -1;
  for (int a = 1; a < argc; a++) {
    if (!strcmp(argv[a], "--version")) {
      puts(VERSION);
      return 0;
    }
    if (!strcmp(argv[a], "--self-test"))
      return selftest();
    if (!strcmp(argv[a], "--supervise")) {
      continue;
    }
    if (!strcmp(argv[a], "--validate")) {
      validate = 1;
      continue;
    }
    if (a + 1 >= argc)
      return 2;
    const char *v = argv[++a];
    if (!strcmp(argv[a - 1], "--root")) {
      if (textset(base, sizeof base, v))
        return 2;
    } else if (!strcmp(argv[a - 1], "--assets")) {
      if (textset(assets, sizeof assets, v))
        return 2;
    } else if (!strcmp(argv[a - 1], "--config")) {
      if (textset(configpath, sizeof configpath, v))
        return 2;
    } else if (!strcmp(argv[a - 1], "--runtime")) {
      if (textset(runtime, sizeof runtime, v) || strlen(runtime) > PATHCAP - 32)
        return 2;
    } else if (!strcmp(argv[a - 1], "--resolve")) {
      if (number(v, 0, 14, &resolvekey))
        return 2;
    } else if (!strcmp(argv[a - 1], "--dry-run")) {
      if (number(v, 0, 14, &dry))
        return 2;
    } else
      return 2;
  }
  if (!configpath[0]) {
    fprintf(stderr, "--config is required\n");
    return 2;
  }
  if (resolvekey >= 0 || dry >= 0 || validate) {
    struct config *c = readconfig(validate);
    if (!c)
      return 2;
    if (validate) {
      size_t total = 0;
      for (int k = 0; k < KEYS; k++)
        for (int s = 0; s < 4; s++)
          if (!c->keys[k].images[s].borrowed)
            total += c->keys[k].images[s].size;
      printf("VALID: 15 configurable buttons; image cache %zu bytes\n", total);
      freeconfig(c);
      return 0;
    }
    char target[PATHCAP];
    int rc = resolve(c, resolvekey >= 0 ? resolvekey : dry, target);
    if (!rc)
      printf("%s%s\n",
             dry < 0                             ? ""
             : c->keys[dry].action == UPDATE_ALL ? "bash "
             : c->keys[dry].action == SCRIPT     ? "sh "
                                                 : "load_core ",
             target);
    else
      fprintf(stderr, "No valid launch target\n");
    freeconfig(c);
    return rc ? 1 : 0;
  }
  if (!assets[0])
    return 2;
  return supervise(worker, 20000);
}
