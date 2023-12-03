#pragma once

#include "types.h"
#include <sys/inotify.h>
#include <linux/limits.h>

typedef struct {
  int fd;
  char* buf;
  i32 buf_len;
  i32 buf_cap;
  i32 event_len;
} watch;

typedef enum {
  watch_event_create = IN_CREATE,
  watch_event_delete = IN_DELETE,
  watch_event_modified = IN_MODIFY,
} watch_event_type;

typedef struct {
  char *name;
  u32 event_type;
} watch_event;

bool watch_init(watch *w);
void watch_free(watch *w);
int watch_add(watch *w, const char *path);
void watch_remove(watch *w, int handle);
bool watch_poll(watch *w, watch_event *event);
void watch_event_free(watch* w, watch_event *event);
