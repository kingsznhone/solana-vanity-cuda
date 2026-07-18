#ifndef SEARCH_MATCH_RECORD_H
#define SEARCH_MATCH_RECORD_H

struct match_record
{
	int gpu;
	unsigned char seed[32];
	unsigned char public_key[32];
};

#endif