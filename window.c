#include "window.h"
#include <GLFW/glfw3.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <logger.h>

void glfw_callback(int error, const char *desc) {
  LOG_ERROR("GLFW error callback: %d (%s)", error, desc);
}

static void set_glfw_callback() {
  static bool set = false;
  if (set) {
    return;
  }

  glfwSetErrorCallback(glfw_callback);
  set = true;
}

static i32 glfw_ref_count = 0;
bool window_init(window *w, i32 width, i32 height, const char *title) {
  assert(glfw_ref_count >= 0);
  if (glfw_ref_count++ == 0) {
    set_glfw_callback();
    if (!glfwInit()) {
      LOG_ERROR("error: unable to initialize GLFW");
      return false;
    }
  }

  glfwDefaultWindowHints();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  w->window = glfwCreateWindow(width, height, title, NULL, NULL);
  return w->window != NULL;
}

void window_free(window *w) {
  glfwDestroyWindow(w->window);

  assert(glfw_ref_count > 0);
  if (--glfw_ref_count == 0) {
    glfwTerminate();
  }
}

bool window_should_close(window *w) { return glfwWindowShouldClose(w->window); }

void window_poll_events() {
  glfwPollEvents();
}
