#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "append_string.h"

AppendString* as_init(AppendString* as) {
  if(!as) {
    as = malloc(sizeof(AppendString));
    if(!as) return NULL;
    as->ownership = 1;
  }
  as->str = calloc(AS_CHUNK, sizeof(char));
  as->sz = AS_CHUNK;
  return as;
}

AppendString* as_free(AppendString* as) {
  if(!as) return NULL;
  free(as->str);
  if(as->ownership) free(as);
  return as->ownership ? NULL : as;
}

// This is tricky, since we want to release the string, but also get new memory
// * If we release first, the overwhelming likelihood is that we'll be able to
//   allocate new pages from the released pool
// * If we release first, then the subsequent allocation can use the free list
//   rather than requiring a new OS request (in the worst-case)
// * Really, what's the actual likelihood that release->alloc would fail but
//   alloc->release would succeed?
char as_clear(AppendString* as) {
  if(!as) return -1;
  free(as->str);
  as->str = calloc(AS_CHUNK, sizeof(char));
  return as->str ? 0 : -1;
}

inline static char _as_grow(AppendString* as, size_t len) {
  // TODO-- could stand to zero out the data, probably
  size_t sz = as->n + len;
  if(as->sz < sz) {
    size_t chunks = 1+(sz+1)/AS_CHUNK;
    char* buf = realloc(as->str, chunks*AS_CHUNK);
    if (!buf) return -1;
    as->str = buf;
    as->sz = chunks*AS_CHUNK;
  }
  return 0;
}

char as_grow(AppendString* as, size_t len) {
  return _as_grow(as, len);
}

char as_add(AppendString* as, const unsigned char* str, size_t sz) {
  if(_as_grow(as, sz))
    return -1;
  memcpy(&as->str[as->n], str, sz);
  as->n += sz;
  return 0;
}

char as_sprintf(AppendString* as, const char* format, ...) {
  va_list arg;
  va_start(arg, format);
  size_t sz = 2+vsnprintf(NULL, 0, format, arg);
  unsigned char* buf = malloc(sz);
  vsnprintf((char*)buf, sz, format, arg);
  va_end(arg);

  char ret = as_add(as, buf, sz);
  free(buf);
  return ret;
}
