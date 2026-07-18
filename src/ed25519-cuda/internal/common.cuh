#ifndef ED25519_CUDA_INTERNAL_COMMON_CUH
#define ED25519_CUDA_INTERNAL_COMMON_CUH

#include <stdint.h>

static uint64_t __host__ __device__ load_3(const unsigned char *input)
{
	uint64_t result = static_cast<uint64_t>(input[0]);
	result |= static_cast<uint64_t>(input[1]) << 8;
	result |= static_cast<uint64_t>(input[2]) << 16;
	return result;
}

static uint64_t __host__ __device__ load_4(const unsigned char *input)
{
	uint64_t result = static_cast<uint64_t>(input[0]);
	result |= static_cast<uint64_t>(input[1]) << 8;
	result |= static_cast<uint64_t>(input[2]) << 16;
	result |= static_cast<uint64_t>(input[3]) << 24;
	return result;
}

#endif
