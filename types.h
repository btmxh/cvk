#pragma once

#include <inttypes.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint8_t u8;
typedef int32_t i32;
typedef int64_t i64;
typedef uint32_t u32;
typedef size_t usize;

#ifdef NDEBUG
#define debug false
#else
#define debug true
#endif

