#pragma once
#include "../core/core.h"
#include <functional>
#include <unordered_map>
#include <string>
/*
* World save requires the following functions to exist in the DM code:
* --------------------------------------------------------------------
* /proc/ping            : Test proc that will be hooked on load.
* /proc/GetSavePath     : Should return a string with the path to save world data to.
* /datum/proc/getRef    : Returns "\ref[src]" so we can sort out things by ref.
*/


//Functions for handling world saving
namespace world_save
{
    //----------------------------
    // Interface functions
    //----------------------------
    //Called from the byond code to let us hook into things and setup the system
    //const char * init_world_save();

    //---------------------------
    //State Object
    //---------------------------
    //Singleton for keeping track of the WorldSave system
    class WorldSaveImpl;
    class WorldSave
    {
    public:
        typedef ProcHook hookedfun_t;
        static WorldSave& Instance();
        WorldSave();
        ~WorldSave();

    public:
        const char* Initialise(int n_args, const char** args);
        bool AddProcToHook(const char* fpath, hookedfun_t&& fun);

        const char* Save();
        const char* Load();

    private:
        std::unique_ptr<WorldSaveImpl> m_pimpl;
    };

};