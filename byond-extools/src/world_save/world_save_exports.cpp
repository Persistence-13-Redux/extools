//C interface for exposing symbols through the shared lib
#include "../core/core.h"
#include "world_save.h"

extern "C" EXPORT const char* init_world_save(int n_args, const char** args)
{
    if (!Core::initialize())
    {
        return "Extools Init Failed";
    }
    return world_save::WorldSave::Instance().Initialise(n_args, args);
}

extern "C" EXPORT const char* save_world_save(int n_args, const char** args)
{
    return world_save::WorldSave::Instance().Save();
}

extern "C" EXPORT const char* load_world_save(int n_args, const char** args)
{
    return world_save::WorldSave::Instance().Load();
}
