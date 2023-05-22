#ifndef _G_BFF_
#define _G_BFF_

#pragma once

typedef struct bff_level_s bff_level_t;

#define HEADER_MAGIC 0x5f3759df
#define BFF_STR_SIZE (int)80

typedef struct bff_texture_s
{
    char *buffer;
    uint64_t fsize;
} bff_texture_t;

/*
* CAN BE USED FOR THE FOLLOWING TYPES OF BINARY DATA:
* - glslang SPIRV (pre-processed OpenGL/Vulkan shader stuff)
* - .so/.dll or .a/.lib, essentially compiled c++ scripted events
*/
typedef struct bff_binary_s
{
	uint8_t type;
	size_t fsize;
	char *buffer;
} bff_binary_t;

typedef struct bff_spawn_s
{
    char entityid[BFF_STR_SIZE+1];
	sprite_t replacement;
	sprite_t marker;
    coord_t where;
	uint8_t what;
	
	void *heap;
} bff_spawn_t;


enum : uint8_t
{
    FT_OGG,
    FT_WAV,
    FT_FLAC,
    FT_OPUS
};

typedef struct bff_script_s
{
    uint64_t fsize;
    char *filebuf;

    std::vector<std::string> funclist;
} bff_script_t;

typedef struct bff_audio_s
{
    // used in file i/o
	int32_t lvl_index; // equal to -1 if not a level-specific track
    uint8_t type;
	uint64_t fsize;
    char *filebuf;
} bff_audio_t;

typedef struct bff_level_s
{
	sprite_t lvl_map[NUMSECTORS][SECTOR_MAX_Y][SECTOR_MAX_X];
	uint16_t *spawnlist;
    uint16_t spawncount = 0;

	bff_spawn_t *spawns;
} bff_level_t;
typedef struct bffinfo_s
{
	uint64_t magic = HEADER_MAGIC;
	uint16_t numlevels;
	uint16_t numspawns;
    uint16_t numtextures;
	uint16_t numsounds;
} bffinfo_t;

typedef struct bff_file_s
{
	FILE* fp;
	bffinfo_t header;
	bff_spawn_t* spawns;
	bff_audio_t* sounds;
	bff_level_t* levels;
    bff_texture_t* textures;
} bff_file_t;

extern uint64_t extra_heap;
extern bff_file_t* bff;
extern bff_level_t* levels;
extern bff_texture_t* textures;
extern bff_spawn_t* spawns;
extern bffinfo_t bffinfo;

void G_LoadBFF(const std::string& bffname);
void G_ExtractBFF(const std::string& filepath);
void G_WriteBFF(const char* outfile, const char* dirname);

#endif