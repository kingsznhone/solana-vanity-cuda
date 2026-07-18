#include <stddef.h>
#include <string.h>

#include <cuda_runtime.h>
#include "curand_kernel.h"

#include "../config.h"
#include "../ed25519-cuda/group/ge.h"
#include "../ed25519-cuda/hash/sha512.h"
#include "match_record.h"
#include "base58_prefix.cuh"
#include "vanity_kernel.cuh"

__constant__ prefix_range device_prefix_ranges[MAX_PATTERNS];
__constant__ int device_prefix_count;

static __device__ __forceinline__ void vanity_scalarmult_base(
	ge_p3 *point, const unsigned char *scalar, const ge_precomp *fixed16_table)
{
	ge_scalarmult_base_fixed16_clamped(point, scalar, fixed16_table);
}

void __global__ vanity_init(unsigned long long int *rseed, curandState *state)
{
	int id = threadIdx.x + (blockIdx.x * blockDim.x);
	curand_init(*rseed + id, id, 0, &state[id]);
}

void __global__ VANITY_KERNEL_LAUNCH_BOUNDS vanity_scan(
	curandState *state, unsigned long long *keys_found, int *gpu,
	match_record *matches, unsigned long long *match_count,
	const ge_precomp *fixed16_table)
{
	int id = threadIdx.x + (blockIdx.x * blockDim.x);

	ge_p3 point;
	curandState local_state = state[id];
	unsigned char seed[32] = {0};
	unsigned char public_key[32] = {0};
	unsigned char private_key[32] = {0};
	char encoded_key[45] = {0};

	for (int index = 0; index < 32; ++index)
	{
		float random_value = curand_uniform(&local_state);
		seed[index] = static_cast<unsigned char>(random_value * 255);
	}

	for (int attempt = 0; attempt < VANITY_ATTEMPTS_PER_EXECUTION; ++attempt)
	{
		sha512_32_first_half(seed, private_key);

		private_key[0] &= 248;
		private_key[31] &= 63;
		private_key[31] |= 64;

		vanity_scalarmult_base(&point, private_key, fixed16_table);
		ge_p3_tobytes(public_key, &point);

		int matched_prefix = matching_prefix_range(public_key);
		if (matched_prefix >= 0 || needs_base58_fallback(public_key))
		{
			size_t key_size = sizeof(encoded_key);
			if (base58_encode_device(encoded_key, &key_size, public_key, sizeof(public_key)))
			{
				if (matched_prefix < 0)
				{
					for (int index = 0; index < device_prefix_count; ++index)
					{
						if (matches_exact_prefix(encoded_key, device_prefix_ranges[index]))
						{
							matched_prefix = index;
							break;
						}
					}
				}

				if (matched_prefix >= 0)
				{
					atomicAdd(keys_found, 1ULL);
					unsigned long long match_index = atomicAdd(
						match_count, 1ULL);
					if (match_index < MAX_MATCHES_PER_LAUNCH)
					{
						matches[match_index].gpu = *gpu;
						memcpy(matches[match_index].seed, seed, sizeof(seed));
						memcpy(matches[match_index].public_key, public_key, sizeof(public_key));
					}
				}
			}
		}

		for (int index = 0; index < 32; ++index)
		{
			if (seed[index] == 255)
			{
				seed[index] = 0;
			}
			else
			{
				seed[index] += 1;
				break;
			}
		}
	}

	state[id] = local_state;
}