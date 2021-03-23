#include "../core/core.h"
#include "../third_party/json.hpp"
#include "world_save.h"
#include <vector>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <filesystem>
#include <sstream>
#include <fstream>

using json = nlohmann::json;

//Helper struct that adds a function hook to the list of function to hook on startup
struct FunDefiner
{
    FunDefiner(const char* fpath, world_save::WorldSave::hookedfun_t&& fn)
    {
        world_save::WorldSave::Instance().AddProcToHook(fpath, std::forward<world_save::WorldSave::hookedfun_t>(fn));
    }
};

//Helper macro to hook functions
#define DEFINE_HOOKED_FUNCTION(BYONDFPATH, FNAME)\
    trvh FNAME(unsigned int args_len, Value* args, Value src);\
    const FunDefiner FNAME##_def(BYONDFPATH, world_save::WorldSave::hookedfun_t(&FNAME));\
    trvh FNAME(unsigned int args_len, Value* args, Value src)\


namespace world_save
{
    const char* RETURN_SUCCESS = "ok";
    const char* RETURN_FAILED = "failed";
    const char* SAVE_FILE_EXTENSION = "spsf";
    const char* SAVE_FILE_FORMAT_ZLEVEL = "z%i.spsf";
    const char* SAVE_FILE_NAME_INSTANCES = "instances";

    //----------------------------------------
    //Implementation for WorldSave object
    //----------------------------------------
    class WorldSaveImpl
    {
    public:
        using hookedfun_t = WorldSave::hookedfun_t;

        WorldSaveImpl()
        {
        }

        ~WorldSaveImpl()
        {
        }

        const char* Initialise(int n_args, const char** args)
        {
            //Get all globals
            m_globals = Value::Global().get_all_vars();

            //Hook functions
            for(const auto & entry : m_procstohook)
                Core::get_proc(entry.first).hook(entry.second);

            return RETURN_SUCCESS;
        }

        bool AddProcToHook(const char* fpath, hookedfun_t&& fun)
        {
            auto result = m_procstohook.try_emplace(std::string(fpath), fun);
            return result.second;
        }

        const char* Save()
        {
            //First grab the path to the save
            Core::Proc * pProc = Core::try_get_proc("/proc/GetSavePath");
            if (!pProc)
            {
                //maybe emit an error message
                return RETURN_FAILED;
            }
            std::string fpath = pProc->call({});
            
            //Create dirs if needed
            std::filesystem::create_directories(fpath);

            Value world = m_globals["world"];
            int maxx = world.get("maxx");
            int maxy = world.get("maxy");
            int maxz = world.get("maxz");

            //Create output
            if (!make_save_files(fpath, maxz))
                return RETURN_FAILED;


            //Core::get_turf(1,1,1);

            //Contains all the json data for everything saved, including z-levels and instances
            // -> zlevle files contains turfs
            // -> instances contains everything else
            json saved;

            Container worldcnt = world.get("contents");
            for(size_t i = 0; i < worldcnt.length(); ++i)
            {
                save_world_item(worldcnt.at(i), saved);
                //Iterate through all instances in the world cooperatively all z-levels in "parallel"
                    //Write instances by refid in the target file, replace all instance references by refid
                    //Be sure to write value type
            }

            //Save instances
            std::vector<uint8_t> instances = json::to_msgpack(saved["instances"]);
            std::copy(instances.begin(), instances.end(), std::ostreambuf_iterator(m_instancesbuf));

            //Save z-level stuff
            std::vector<std::vector<uint8_t>> zlevels;
            std::array<char, 16> znamebuff;
            for (size_t i = 0; i < maxz; ++i)
            {
                sprintf(znamebuff.data(), "z%i", i);
                const std::string zlevelid = zlevel_json_id(i, znamebuff.data(), znamebuff.size());
                zlevels[i] = json::to_msgpack(saved[zlevelid]);
                //Write to file
                std::copy(zlevels[i].begin(), zlevels[i].end(), std::ostreambuf_iterator(m_zlevelbufs[i]));
            }

            return RETURN_SUCCESS;
        }

        const char* Load()
        {
            //First grab the path to the save

            //
            return RETURN_SUCCESS;
        }

    private:

        static std::string zlevel_json_id(unsigned int levelid, char * znamebuff, size_t znamebuflen)
        {
            sprintf(znamebuff, "z%i", levelid);
            return std::string (znamebuff, strnlen(znamebuff, znamebuflen));
        }

        std::filesystem::path zlevel_fname(const std::string & path, size_t zlvl)const
        {
            std::stringstream sstr;
            sstr << "z" << zlvl << "." << SAVE_FILE_EXTENSION;
            return std::filesystem::path(path).append(sstr.str());
        }

        std::filesystem::path instances_path(const std::string& path)const
        {
            std::stringstream sstr;
            sstr << SAVE_FILE_NAME_INSTANCES << "." << SAVE_FILE_EXTENSION;
            return std::filesystem::path(path).append(sstr.str());
        }

        //backup the saved files under this path
        bool backup_files(const std::string& path)
        {
            if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path))
                return false;

            if (!std::filesystem::is_empty(path))
            {
                std::filesystem::path backuppath = std::filesystem::path(path).parent_path().append("_save_backup");
                std::filesystem::create_directory(backuppath);
                std::filesystem::copy(path, backuppath, std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::recursive);
            }
            return true;
        }

        //Make/overwrite/backup save files 
        bool make_save_files(const std::string & path, size_t nbzlvl)noexcept
        {
            std::array<char, 64> charbuff;
            try
            {
                backup_files(path);

                //Create/overwrite files
                m_zlevelbufs.clear();
                m_zlevelbufs.resize(nbzlvl);
                for (size_t i = 1; i <= nbzlvl; ++i)
                {
                    std::filesystem::path strpath = zlevel_fname(path, i);
                    std::filesystem::remove(strpath);
                    m_zlevelbufs[i] = std::fstream(strpath, std::ios_base::out);
                }

                //
                const std::filesystem::path instancespath = instances_path(path);
                std::filesystem::remove(instancespath);
                m_instancesbuf = std::fstream(instancespath, std::ios_base::out);
            }
            catch (...)
            {
                return false;
            }
            return true;
        }

        //Save instances, not variables that links to them!!! important distinction here.
        void save_world_item(Value & atom, json & curstruct)
        {
            switch (atom.type)
            {
            //primitives:
            case DataType::NULL_D:
                {
                    break;
                }
            case DataType::NUMBER:
                {
                    save_number(atom, curstruct);
                    break;
                }

            case DataType::STRING:
            case DataType::AREA_TYPEPATH:
            case DataType::CLIENT_TYPEPATH:
            case DataType::DATUM_TYPEPATH:
            case DataType::IMAGE_TYPEPATH:
            case DataType::LIST_TYPEPATH:
            case DataType::MOB_TYPEPATH:
            case DataType::OBJ_TYPEPATH:
            case DataType::SAVEFILE_TYPEPATH:
            case DataType::TURF_TYPEPATH:
            {
                break;
            }

            //Shit we don't save
            case DataType::APPEARANCE:
            case DataType::FILE_:
            case DataType::FILTERS:
            case DataType::IMAGE:
            case DataType::PREFAB:
            case DataType::RESOURCE:
            case DataType::SAVEFILE:
            {
                break;
            }

            //lists
            case DataType::LIST:
            case DataType::LIST_AREA_CONTENTS:
            case DataType::LIST_AREA_VARS:
            case DataType::LIST_ARGS:
            case DataType::LIST_CONTENTS:
            case DataType::LIST_GLOBAL_VARS:
            case DataType::LIST_GROUP:
            case DataType::LIST_MOB_CONTENTS:
            case DataType::LIST_MOB_VARS:
            case DataType::LIST_OBJ_VARS:
            case DataType::LIST_OVERLAYS:
            case DataType::LIST_TURF_CONTENTS:
            case DataType::LIST_TURF_VARS:
            case DataType::LIST_VARS:
            case DataType::LIST_WORLD_CONTENTS:
            case DataType::LIST_WORLD_VARS:
                {
                    break;
                }

            //Things
            case DataType::DATUM:
            case DataType::MOB:
            case DataType::OBJ:
                {
                    break;
                }

            case DataType::AREA:
                {
                    break;
                }

            case DataType::TURF:
                {
                    break;
                }
            };
        }


        void save_datum(Value & atom, json& curstruct)
        {
            unsigned int refid = atom.invoke("getRef", {}); //Get the refid string or number and use it in the instances table
            auto itf = m_instancestable.find(refid);
            if (itf != m_instancestable.end())
                return; //We don't save instances that have been saved already

            std::unordered_map<std::string, Value> vars = atom.get_all_vars();
            
            //check vars that aren't saved, and skip them
        }

        void save_number(Value& num, json& curstruct)
        {
        }

        void save_string(Value& str, json& curstruct)
        {
        }

        void save_list(Container& list, json& curstruct)
        {
        }

        void save_area(Value& list, json& curstruct)
        {
        }

        void save_turf(Value& list, json& curstruct)
        {
        }

    private:
        std::unordered_map<std::string, hookedfun_t> m_procstohook;

        //Hooked globals
        std::unordered_map<std::string, Value> m_globals;

        //buffer
        std::fstream               m_instancesbuf;
        std::vector<std::fstream>  m_zlevelbufs;
        std::unordered_map<unsigned int, Value> m_instancestable; //Keep track of instances refid assigned to each instances.
    };

    //
    // WorldSave definition
    //
    inline WorldSave& WorldSave::Instance()
    {
        static WorldSave s_ws;
        return s_ws;
    }

    inline WorldSave::WorldSave()
    {
    }

    WorldSave::~WorldSave()
    {
    }

    const char* WorldSave::Initialise(int n_args, const char** args)            {return m_pimpl->Initialise(n_args, args);}
    bool        WorldSave::AddProcToHook(const char* fpath, hookedfun_t&& fun)  {return m_pimpl->AddProcToHook(fpath, std::forward<hookedfun_t>(fun));}
    const char* WorldSave::Save() { return m_pimpl->Save(); }
    const char* WorldSave::Load() { return m_pimpl->Load(); }


    //---------------------------
    //  Interface functions
    //---------------------------

    // Utilities
    DEFINE_HOOKED_FUNCTION("/proc/ping", ping)
    {
        return Value("pong");
    }

    // list

    // matrix

    // datum
    DEFINE_HOOKED_FUNCTION("/datum/proc/Read", datum_read)
    {
        return Value::Null();
    }

    DEFINE_HOOKED_FUNCTION("/datum/proc/Write", datum_write)
    {
        return Value::Null();
    }

    // atom

    // atom/movable

    // mob

    // turf
};
#undef DEFINE_HOOKED_FUNCTION;
