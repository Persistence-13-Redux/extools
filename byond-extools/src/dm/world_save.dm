///////////////////////////////////////////////
//	System Procs
///////////////////////////////////////////////
/proc/WorldSave_Save()
	return call(EXTOOLS, "save_world_save")()

/proc/WorldSave_Load()
	return call(EXTOOLS, "load_world_save")()

///////////////////////////////////////////////
//	Implement required DLL Functions
///////////////////////////////////////////////

//Directory where world saves are stored
/proc/GetSavePath()
	return "data/world_save"

//Return ref id for this datum
/datum/proc/getRef()
	return "\ref[src]"

///////////////////////////////////////////////
//	DLL Hooks
///////////////////////////////////////////////

//Test proc
/proc/ping()
	admin_notice("World Save Lib was not hooked correctly!")
