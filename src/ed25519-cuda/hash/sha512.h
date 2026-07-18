#ifndef SHA512_H
#define SHA512_H

#include "../internal/fixedint.h"

void __device__ __host__ sha512_32_first_half(const unsigned char *message, unsigned char *out);

#endif
