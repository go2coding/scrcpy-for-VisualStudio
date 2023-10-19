#include "memory.h"

#include <stdlib.h>
#include <errno.h>

bool mul_overflow_c(size_t nmemb, size_t size, size_t* bytes)
{
    if (nmemb != 0 && size > (unsigned long long)(-1) / nmemb) {
        return true;
    }

    *bytes = nmemb * size;
    return false;
}

void *
sc_allocarray(size_t nmemb, size_t size) {
    size_t bytes;
    /*
    if (__builtin_mul_overflow(nmemb, size, &bytes)) {
      errno = ENOMEM;
      return NULL;
    }
    */
    if (mul_overflow_c(nmemb, size, &bytes)) {
        return NULL;
    }

    return malloc(bytes);
}
