#ifndef SEARCH_VANITY_KERNEL_CUH
#define SEARCH_VANITY_KERNEL_CUH

#include <cuda_runtime.h>
#include "curand_kernel.h"

#include "../config.h"
#include "../ed25519-cuda/group/ge.h"
#include "match_record.h"

#if defined(__CUDACC__) && VANITY_USE_LAUNCH_BOUNDS
#define VANITY_KERNEL_LAUNCH_BOUNDS __launch_bounds__(VANITY_THREADS_PER_BLOCK)
#else
#define VANITY_KERNEL_LAUNCH_BOUNDS
#endif

static int const MAX_MATCHES_PER_LAUNCH = 64;

void __global__ vanity_init(unsigned long long int *seed, curandState *state);
void __global__ VANITY_KERNEL_LAUNCH_BOUNDS vanity_scan(
	curandState *state, unsigned long long *keys_found, int *gpu,
	match_record *matches, unsigned long long *match_count,
	const ge_precomp *fixed16_table);

#endif
