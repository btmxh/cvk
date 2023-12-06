#include "watch_linux.h"
#include "types.h"
#include <errno.h>
#include <logger.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <unistd.h>

bool watch_init(watch *w) {
  w->fd = inotify_init1(IN_NONBLOCK);
  if (w->fd == -1) {
    LOG_ERROR("inotify initialization failed with error: %s", strerror(errno));
    return false;
  }

  w->buf_len = 0;
  w->buf_cap = sizeof(struct inotify_event) + PATH_MAX + 1;
  w->buf = calloc(w->buf_cap, 1);
  if (!w->buf) {
    LOG_ERROR("unable to allocate inotify read buffer");
    if (close(w->fd) == -1) {
      LOG_WARN("closing inotify fd failed with error: %s", strerror(errno));
    }
  }

  return true;
}

void watch_free(watch *w) {
  free(w->buf);
  if (close(w->fd) == -1) {
    LOG_WARN("closing inotify fd failed with error: %s", strerror(errno));
  }
}

int watch_add(watch *w, const char *path) {
  int handle =
      inotify_add_watch(w->fd, path, IN_CREATE | IN_DELETE | IN_MODIFY);
  if (handle == -1) {
    LOG_WARN("adding file to inotify failed with error: %s", strerror(errno));
  }

  return handle;
}

void watch_remove(watch *w, int handle) { inotify_rm_watch(w->fd, handle); }

bool watch_poll(watch *w, watch_event *event) {
  ssize_t r;
  struct inotify_event *e = (struct inotify_event *)w->buf;
  while (w->buf_len < (i32)sizeof(struct inotify_event) ||
         w->buf_len < (i32)(sizeof(struct inotify_event) + e->len)) {
    if ((r = read(w->fd, w->buf, w->buf_cap)) == -1) {
      return false;
    }

    w->buf_len += r;
  }

  event->name = e->name;
  event->event_type = e->mask;
  w->event_len = e->len;
  return true;
}

void watch_event_free(watch *w, watch_event *event) {
  (void)event;
  i32 offset = sizeof(struct inotify_event) + w->event_len;
  memmove(w->buf, &w->buf[offset], w->buf_cap - offset);
  w->buf_len -= offset;
}
