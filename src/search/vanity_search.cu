#include <vector>
#include <random>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iomanip>

#include <iostream>
#include <ctime>
#include <sstream>

#include <stdio.h>
#include <string.h>

#include <cuda_runtime.h>
#include "curand_kernel.h"
#include "../ed25519-cuda/internal/fixedint.h"
#include "../ed25519-cuda/field/fe.h"
#include "../ed25519-cuda/group/ge.h"
#include "../ed25519-cuda/hash/sha512.h"
#include "gpu_common.h"

#include "../config.h"
#include "fixed16_table_loader.h"
#include "match_record.h"
#include "result_writer.h"
#include "vanity_search.h"
#include "vanity_kernel.cuh"
#include "base58_prefix.cuh"

/* -- Types ----------------------------------------------------------------- */

typedef struct
{
	curandState *states[8];
	int *device_gpu_ids[8];
	unsigned long long *device_keys_found[8];
	unsigned long long *device_match_counts[8];
	match_record *device_matches[8];
	std::vector<match_record> host_matches[8];
	ge_precomp *device_fixed16_tables[8];
	int block_sizes[8];
	int grid_sizes[8];
	int gpu_count;
} config;

static volatile std::sig_atomic_t stop_requested = 0;

static void request_stop(int)
{
	stop_requested = 1;
}

/* -- Prototypes, Because C++ ----------------------------------------------- */

bool vanity_setup(config &vanity, bool deterministic, const vanity_options &options);
bool vanity_run(config &vanity, const vanity_options &options);
void vanity_cleanup(config &vanity);
/* -- Engine API ------------------------------------------------------------ */

int run_vanity_search(const vanity_options &options)
{
	stop_requested = 0;
	struct sigaction action = {};
	struct sigaction previous_sigint = {};
	struct sigaction previous_sigterm = {};
	action.sa_handler = request_stop;
	sigemptyset(&action.sa_mask);
	sigaction(SIGINT, &action, &previous_sigint);
	sigaction(SIGTERM, &action, &previous_sigterm);

	if (!initialize_result_writer())
	{
		sigaction(SIGINT, &previous_sigint, NULL);
		sigaction(SIGTERM, &previous_sigterm, NULL);
		return 1;
	}

	config vanity = {};
	if (!vanity_setup(vanity, false, options))
	{
		sigaction(SIGINT, &previous_sigint, NULL);
		sigaction(SIGTERM, &previous_sigterm, NULL);
		return 1;
	}
	bool success = vanity_run(vanity, options);
	vanity_cleanup(vanity);
	sigaction(SIGINT, &previous_sigint, NULL);
	sigaction(SIGTERM, &previous_sigterm, NULL);
	return success ? 0 : 1;
}

std::string getTimeStr()
{
	std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::string s(30, '\0');
	std::strftime(&s[0], s.size(), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
	return s;
}

std::string format_attempts(std::uint64_t attempts)
{
	std::ostringstream output;
	output << std::fixed << std::setprecision(2)
		   << static_cast<double>(attempts) / 1000000.0 << "M";
	return output.str();
}

unsigned long long int makeSeed()
{
	unsigned long long int seed = 0;
	char *pseed = (char *)&seed;

	std::random_device rd;

	for (unsigned int b = 0; b < sizeof(seed); b++)
	{
		auto r = rd();
		char *entropy = (char *)&r;
		pseed[b] = entropy[0];
	}

	return seed;
}

/* -- Vanity Step Functions ------------------------------------------------- */

bool vanity_setup(config &vanity, bool deterministic, const vanity_options &options)
{
	printf("GPU: Initializing Memory\n");
	fixed16_host_table fixed16_host = {};
	if (!fixed16_generate_host_table(fixed16_host))
	{
		return false;
	}
	cudaGetDeviceCount(&vanity.gpu_count);
	if (vanity.gpu_count <= 0 || vanity.gpu_count > 8)
	{
		fprintf(stderr, "Expected between 1 and 8 visible CUDA devices, found %d\n", vanity.gpu_count);
		return false;
	}
	assert(options.prefix_count > 0 && options.prefix_count <= MAX_PATTERNS);
	prefix_range host_ranges[MAX_PATTERNS] = {};
	for (int i = 0; i < options.prefix_count; ++i)
	{
		build_prefix_range(options.prefixes[i], host_ranges[i]);
		printf("Prefix: %s (leading ones: %d)\n", host_ranges[i].prefix, host_ranges[i].leading_ones);
	}

	// Create random states so kernels have access to random generators
	// while running in the GPU.
	for (int i = 0; i < vanity.gpu_count; ++i)
	{
		cudaSetDevice(i);
		if (!fixed16_upload_table(i, fixed16_host,
								  reinterpret_cast<void **>(&vanity.device_fixed16_tables[i])))
		{
			for (int uploaded = 0; uploaded < i; ++uploaded)
			{
				cudaSetDevice(uploaded);
				cudaFree(vanity.device_fixed16_tables[uploaded]);
				vanity.device_fixed16_tables[uploaded] = NULL;
			}
			return false;
		}
		cudaMemcpyToSymbol(device_prefix_ranges, host_ranges, sizeof(host_ranges));
		int active_prefix_count = deterministic ? 0 : options.prefix_count;
		cudaMemcpyToSymbol(device_prefix_count, &active_prefix_count, sizeof(active_prefix_count));

		// Fetch Device Properties
		cudaDeviceProp device;
		cudaGetDeviceProperties(&device, i);

		if (VANITY_THREADS_PER_BLOCK > device.maxThreadsPerBlock)
		{
			fprintf(stderr, "GPU %d supports at most %d threads per block; build requests %d\n",
					i, device.maxThreadsPerBlock, VANITY_THREADS_PER_BLOCK);
			return false;
		}
		int maxActiveBlocks = 0;
		cudaError_t occupancy_status = cudaOccupancyMaxActiveBlocksPerMultiprocessor(
			&maxActiveBlocks, vanity_scan, VANITY_THREADS_PER_BLOCK, 0);
		if (occupancy_status != cudaSuccess)
		{
			fprintf(stderr, "GPU %d cannot launch the configured kernel: %s\n",
					i, cudaGetErrorString(occupancy_status));
			return false;
		}
		if (maxActiveBlocks == 0)
		{
			fprintf(stderr,
					"GPU %d resource configuration permits zero active blocks/SM "
					"(registers=%s, block=%d)\n",
					i, VANITY_REGISTER_LIMIT, VANITY_THREADS_PER_BLOCK);
			return false;
		}
		vanity.block_sizes[i] = VANITY_THREADS_PER_BLOCK;
		vanity.grid_sizes[i] = maxActiveBlocks * device.multiProcessorCount;

		// Output Device Details
		//
		// Our kernels currently don't take advantage of data locality
		// or how warp execution works, so each thread can be thought
		// of as a totally independent thread of execution (bad). On
		// the bright side, this means we can really easily calculate
		// maximum occupancy for a GPU because we don't have to care
		// about building blocks well. Essentially we're trading away
		// GPU SIMD ability for standard parallelism, which CPUs are
		// better at and GPUs suck at.
		//
		// Next Weekend Project: ^ Fix this.
		printf("GPU: %d (%s) -- Registers: %s, Block: %d, Active blocks/SM: %d, Grid: %d, SMs: %d\n",
			   i,
			   device.name,
			   VANITY_REGISTER_LIMIT,
			   vanity.block_sizes[i],
			   maxActiveBlocks,
			   vanity.grid_sizes[i],
			   device.multiProcessorCount);

		// the random number seed is uniquely generated each time the program
		// is run, from the operating system entropy

		unsigned long long int rseed = deterministic ? 0x6a09e667f3bcc908ULL : makeSeed();
		printf("Initialising from %s seed: %llu\n", deterministic ? "deterministic" : "entropy", rseed);

		unsigned long long int *dev_rseed;
		cudaMalloc((void **)&dev_rseed, sizeof(unsigned long long int));
		cudaMemcpy(dev_rseed, &rseed, sizeof(unsigned long long int), cudaMemcpyHostToDevice);

		int thread_count = vanity.grid_sizes[i] * vanity.block_sizes[i];
		cudaMalloc((void **)&(vanity.states[i]), thread_count * sizeof(curandState));
		cudaMalloc((void **)&(vanity.device_gpu_ids[i]), sizeof(int));
		cudaMalloc((void **)&(vanity.device_keys_found[i]),
				   sizeof(unsigned long long));
		cudaMalloc((void **)&(vanity.device_match_counts[i]),
				   sizeof(unsigned long long));
		cudaMalloc((void **)&(vanity.device_matches[i]), MAX_MATCHES_PER_LAUNCH * sizeof(match_record));
		vanity.host_matches[i].resize(MAX_MATCHES_PER_LAUNCH);
		cudaMemcpy(vanity.device_gpu_ids[i], &i, sizeof(int), cudaMemcpyHostToDevice);
		cudaMemset(vanity.device_keys_found[i], 0, sizeof(unsigned long long));
		cudaMemset(vanity.device_match_counts[i], 0, sizeof(unsigned long long));
		vanity_init<<<vanity.grid_sizes[i], vanity.block_sizes[i]>>>(dev_rseed, vanity.states[i]);
		cudaFree(dev_rseed);
		if (cudaDeviceSynchronize() != cudaSuccess)
		{
			fprintf(stderr, "GPU %d initialization failed: %s\n", i, cudaGetErrorString(cudaGetLastError()));
			return false;
		}
	}

	printf("END: Initializing Memory\n");
	return true;
}

bool vanity_run(config &vanity, const vanity_options &options)
{
	std::uint64_t executions_total = 0;
	std::uint64_t executions_this_iteration;

	std::uint64_t keys_found_total = 0;
	std::uint64_t keys_found_this_iteration;
	for (int i = 0; i < options.max_iterations; ++i)
	{
		if (stop_requested)
		{
			printf("Search interrupted, cleaning up GPUs.\n");
			return true;
		}
		auto start = std::chrono::high_resolution_clock::now();

		executions_this_iteration = 0;

		// Run on all GPUs
		for (int g = 0; g < vanity.gpu_count; ++g)
		{
			cudaError_t status = cudaSetDevice(g);
			if (status == cudaSuccess)
			{
				status = cudaMemset(vanity.device_keys_found[g], 0,
									sizeof(unsigned long long));
			}
			if (status == cudaSuccess)
			{
				status = cudaMemset(vanity.device_match_counts[g], 0,
									sizeof(unsigned long long));
			}
			if (status != cudaSuccess)
			{
				fprintf(stderr, "VANITY_CUDA_FAIL,stage=prepare,gpu=%d,error=%s\n",
						g, cudaGetErrorString(status));
				return false;
			}
			vanity_scan<<<vanity.grid_sizes[g], vanity.block_sizes[g]>>>(
				vanity.states[g], vanity.device_keys_found[g], vanity.device_gpu_ids[g],
				vanity.device_matches[g], vanity.device_match_counts[g],
				vanity.device_fixed16_tables[g]);
			status = cudaGetLastError();
			if (status != cudaSuccess)
			{
				fprintf(stderr, "VANITY_CUDA_FAIL,stage=launch,gpu=%d,error=%s\n",
						g, cudaGetErrorString(status));
				return false;
			}
			executions_this_iteration +=
				static_cast<std::uint64_t>(vanity.grid_sizes[g]) *
				static_cast<std::uint64_t>(vanity.block_sizes[g]) *
				static_cast<std::uint64_t>(VANITY_ATTEMPTS_PER_EXECUTION);
		}

		for (int g = 0; g < vanity.gpu_count; ++g)
		{
			cudaError_t status = cudaSetDevice(g);
			if (status == cudaSuccess)
			{
				status = cudaDeviceSynchronize();
			}
			if (status != cudaSuccess)
			{
				fprintf(stderr, "VANITY_CUDA_FAIL,stage=synchronize,gpu=%d,error=%s\n",
						g, cudaGetErrorString(status));
				return false;
			}
		}
		if (stop_requested)
		{
			printf("Search interrupted, cleaning up GPUs.\n");
			return true;
		}
		auto finish = std::chrono::high_resolution_clock::now();

		for (int g = 0; g < vanity.gpu_count; ++g)
		{
			cudaError_t status = cudaSetDevice(g);
			if (status == cudaSuccess)
			{
				status = cudaMemcpy(&keys_found_this_iteration,
									vanity.device_keys_found[g], sizeof(keys_found_this_iteration),
									cudaMemcpyDeviceToHost);
			}
			if (status != cudaSuccess)
			{
				fprintf(stderr, "VANITY_CUDA_FAIL,stage=read_keys,gpu=%d,error=%s\n",
						g, cudaGetErrorString(status));
				return false;
			}
			keys_found_total += keys_found_this_iteration;
			unsigned long long match_count = 0;
			status = cudaMemcpy(&match_count, vanity.device_match_counts[g],
								sizeof(match_count),
								cudaMemcpyDeviceToHost);
			if (status != cudaSuccess)
			{
				fprintf(stderr, "VANITY_CUDA_FAIL,stage=read_matches,gpu=%d,error=%s\n",
						g, cudaGetErrorString(status));
				return false;
			}
			int saved_match_count = static_cast<int>(std::min(
				match_count,
				static_cast<unsigned long long>(MAX_MATCHES_PER_LAUNCH)));
			if (saved_match_count > 0)
			{
				status = cudaMemcpy(vanity.host_matches[g].data(), vanity.device_matches[g],
									saved_match_count * sizeof(match_record), cudaMemcpyDeviceToHost);
				if (status != cudaSuccess)
				{
					fprintf(stderr, "VANITY_CUDA_FAIL,stage=read_match_records,gpu=%d,error=%s\n",
							g, cudaGetErrorString(status));
					return false;
				}
				for (int match = 0; match < saved_match_count; ++match)
				{
					if (!write_match(vanity.host_matches[g][match]))
					{
						return false;
					}
				}
			}
			if (match_count > MAX_MATCHES_PER_LAUNCH)
			{
				fprintf(stderr, "MATCH_BUFFER_OVERFLOW,gpu=%d,found=%llu,saved=%d\n",
						g, static_cast<unsigned long long>(match_count),
						MAX_MATCHES_PER_LAUNCH);
			}
		}
		executions_total += executions_this_iteration;

		// Print out performance Summary
		std::chrono::duration<double> elapsed = finish - start;
		printf("%s Iteration %d Attempts: %s in %f at %.2f MH/s - Total Attempts %s - keys found %llu\n",
			   getTimeStr().c_str(),
			   i + 1,
			   format_attempts(executions_this_iteration).c_str(),
			   elapsed.count(),
			   executions_this_iteration / elapsed.count() / 1000000.0,
			   format_attempts(executions_total).c_str(),
			   static_cast<unsigned long long>(keys_found_total));

		if (keys_found_total >= options.stop_after_keys_found)
		{
			printf("Enough keys found, Done! \n");
			return true;
		}
	}

	printf("Iterations complete, Done!\n");
	return true;
}

void vanity_cleanup(config &vanity)
{
	for (int gpu = 0; gpu < vanity.gpu_count; ++gpu)
	{
		cudaSetDevice(gpu);
		cudaFree(vanity.states[gpu]);
		cudaFree(vanity.device_gpu_ids[gpu]);
		cudaFree(vanity.device_keys_found[gpu]);
		cudaFree(vanity.device_match_counts[gpu]);
		cudaFree(vanity.device_matches[gpu]);
		cudaFree(vanity.device_fixed16_tables[gpu]);
	}
}
