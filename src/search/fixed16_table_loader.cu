#include "fixed16_table_loader.h"

#include <chrono>
#include <stdio.h>
#include <string.h>
#include <string>

#include <cuda_runtime.h>
#include <openssl/evp.h>

#include "../ed25519-cuda/group/ge.h"
#include "fixed16_table.h"
#include "fixed16_table_tool.h"

namespace
{

	std::string sha256_hex(const unsigned char digest[32])
	{
		static const char digits[] = "0123456789abcdef";
		std::string result(64, '0');
		for (int byte = 0; byte < 32; ++byte)
		{
			result[2 * byte] = digits[digest[byte] >> 4];
			result[2 * byte + 1] = digits[digest[byte] & 15];
		}
		return result;
	}

} // namespace

bool fixed16_generate_host_table(fixed16_host_table &table)
{
	static_assert(sizeof(ge_precomp) == FIXED16_TABLE_ENTRY_SIZE,
				  "fixed16 artifact requires 120-byte ge_precomp");
	auto started = std::chrono::steady_clock::now();
	if (!fixed16_generate_payload(table.payload) ||
		table.payload.size() != FIXED16_TABLE_PAYLOAD_SIZE)
	{
		fprintf(stderr, "FIXED16_TABLE_GENERATE_FAIL,invalid payload\n");
		table.payload.clear();
		return false;
	}
	unsigned char digest[32];
	unsigned int digest_size = 0;
	EVP_MD_CTX *context = EVP_MD_CTX_new();
	bool digest_ok = context != NULL &&
					 EVP_DigestInit_ex(context, EVP_sha256(), NULL) == 1 &&
					 EVP_DigestUpdate(context, table.payload.data(), table.payload.size()) == 1 &&
					 EVP_DigestFinal_ex(context, digest, &digest_size) == 1 &&
					 digest_size == 32;
	EVP_MD_CTX_free(context);
	if (!digest_ok)
	{
		fprintf(stderr, "FIXED16_TABLE_GENERATE_FAIL,cannot hash payload\n");
		table.payload.clear();
		return false;
	}
	std::string digest_string = sha256_hex(digest);
	if (digest_string != FIXED16_TABLE_SHA256_HEX)
	{
		fprintf(stderr, "FIXED16_TABLE_GENERATE_FAIL,unexpected deterministic SHA-256\n");
		table.payload.clear();
		return false;
	}
	memcpy(table.sha256, digest_string.c_str(), digest_string.size() + 1);
	double elapsed_ms = std::chrono::duration<double, std::milli>(
							std::chrono::steady_clock::now() - started)
							.count();
	printf("FIXED16_TABLE_GENERATED,payload_bytes=%llu,sha256=%s,elapsed_ms=%.3f\n",
		   static_cast<unsigned long long>(table.payload.size()), table.sha256, elapsed_ms);
	return true;
}

bool fixed16_upload_table(int gpu, const fixed16_host_table &host, void **device_table)
{
	*device_table = NULL;
	cudaError_t status = cudaSetDevice(gpu);
	if (status != cudaSuccess)
	{
		fprintf(stderr, "FIXED16_TABLE_UPLOAD_FAIL,gpu=%d,error=%s\n", gpu, cudaGetErrorString(status));
		return false;
	}
	size_t free_bytes = 0;
	size_t total_bytes = 0;
	status = cudaMemGetInfo(&free_bytes, &total_bytes);
	if (status != cudaSuccess || free_bytes < host.payload.size())
	{
		fprintf(stderr, "FIXED16_TABLE_UPLOAD_FAIL,gpu=%d,free_bytes=%llu,required_bytes=%llu\n",
				gpu, static_cast<unsigned long long>(free_bytes),
				static_cast<unsigned long long>(host.payload.size()));
		return false;
	}
	auto started = std::chrono::steady_clock::now();
	status = cudaMalloc(device_table, host.payload.size());
	if (status == cudaSuccess)
	{
		status = cudaMemcpy(*device_table, host.payload.data(), host.payload.size(), cudaMemcpyHostToDevice);
	}
	if (status != cudaSuccess)
	{
		fprintf(stderr, "FIXED16_TABLE_UPLOAD_FAIL,gpu=%d,error=%s\n", gpu, cudaGetErrorString(status));
		if (*device_table != NULL)
		{
			cudaFree(*device_table);
			*device_table = NULL;
		}
		return false;
	}
	double elapsed_ms = std::chrono::duration<double, std::milli>(
							std::chrono::steady_clock::now() - started)
							.count();
	printf("FIXED16_TABLE_UPLOADED,gpu=%d,bytes=%llu,elapsed_ms=%.3f\n", gpu,
		   static_cast<unsigned long long>(host.payload.size()), elapsed_ms);
	return true;
}