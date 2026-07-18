#ifndef SEARCH_FIXED16_TABLE_LOADER_H
#define SEARCH_FIXED16_TABLE_LOADER_H

#include <vector>

struct fixed16_host_table
{
	std::vector<unsigned char> payload;
	char sha256[65];
};

bool fixed16_generate_host_table(fixed16_host_table &table);
bool fixed16_upload_table(int gpu, const fixed16_host_table &host, void **device_table);

#endif