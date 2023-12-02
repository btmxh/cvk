#include <stdbool.h>
#include "types.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

typedef struct {
  GLFWwindow *window;
} window;

bool window_init(window* w, i32 width, i32 height, const char* title);
void window_free(window* w);

bool window_should_close(window* w);
void window_poll_events();

