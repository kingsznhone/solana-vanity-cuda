#ifndef TOOLS_FIXED16_SHA256_H
#define TOOLS_FIXED16_SHA256_H

#include <cstddef>
#include <string>

class Fixed16Sha256
{
public:
	Fixed16Sha256();
	~Fixed16Sha256();

	Fixed16Sha256(const Fixed16Sha256 &) = delete;
	Fixed16Sha256 &operator=(const Fixed16Sha256 &) = delete;

	void update(const void *data, std::size_t size);
	void finish(unsigned char digest[32]);

private:
	struct evp_md_ctx_st *context_;
};

std::string fixed16_digest_hex(const unsigned char digest[32]);

#endif
