#include "fixed16_sha256.h"

#include <openssl/evp.h>

#include <stdexcept>

Fixed16Sha256::Fixed16Sha256() : context_(EVP_MD_CTX_new())
{
	if (context_ == nullptr || EVP_DigestInit_ex(context_, EVP_sha256(), nullptr) != 1)
	{
		throw std::runtime_error("cannot initialize SHA-256");
	}
}

Fixed16Sha256::~Fixed16Sha256()
{
	EVP_MD_CTX_free(context_);
}

void Fixed16Sha256::update(const void *data, std::size_t size)
{
	if (EVP_DigestUpdate(context_, data, size) != 1)
	{
		throw std::runtime_error("cannot update SHA-256");
	}
}

void Fixed16Sha256::finish(unsigned char digest[32])
{
	unsigned int size = 0;
	if (EVP_DigestFinal_ex(context_, digest, &size) != 1 || size != 32)
	{
		throw std::runtime_error("cannot finish SHA-256");
	}
}

std::string fixed16_digest_hex(const unsigned char digest[32])
{
	static const char digits[] = "0123456789abcdef";
	std::string result(64, '0');
	for (int index = 0; index < 32; ++index)
	{
		result[2 * index] = digits[digest[index] >> 4];
		result[2 * index + 1] = digits[digest[index] & 15];
	}
	return result;
}
