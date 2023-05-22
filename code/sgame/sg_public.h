#ifndef _SG_PUBLIC_
#define _SG_PUBLIC_

#pragma once

#ifdef QVM
#include "qvmstdlib.h"
#include "nomadlib.h"
#endif

typedef enum
{
    SYS_CON_PRINTF = 0,
    SYS_CON_ERROR,
    CON_FLUSH,
    G_PLAYSFX,
    G_GETTILEMAP,
    N_SAVEGAME,
    N_LOADGAME,
    G_UPDATECONFIG,

    NUM_SGAME_IMPORT
} sgameImport_t;

typedef enum
{
    SGAME_INIT,
    SGAME_SHUTDOWN,
    SGAME_STARTLEVEL,
    SGAME_ENDLEVEL,
} sgameExport_t;

void G_UpdateConfig(vmCvar_t* vars);
void G_GetTilemap(sprite_t tilemap[MAP_MAX_Y][MAP_MAX_X]);
void N_SaveGame(uint32_t slot);

#endif