
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "scriptreg.hpp"

#include <string>

#include <angelscript/scriptmath.h>
#include <angelscript/scriptstdstring.h>
#include <angelscript/scriptdictionary.h>
#include <angelscript/scriptfile.h>
#include "./scriptlibc.h"

#include "container/container.hpp"
#include "container/iterator.hpp"
#include "container/ptr_vector.hpp"
#include "container/ptr_list.hpp"

#include "arena.hpp"
#include "config.hpp"
#include "console.hpp"
#include "character.hpp"
#include "eodata.hpp"
#include "eoserver.hpp"
#include "hook.hpp"
#include "map.hpp"
#include "npc.hpp"
#include "party.hpp"
#include "player.hpp"
#include "timer.hpp"
#include "world.hpp"

#ifdef DEBUG
static bool script_DEBUG = true;
#else // DEBUG
static bool script_DEBUG = false;
#endif // DEBUG

static ScriptEngine *engine;

template <class T> static void REG()
{
	T::ScriptRegister(*engine);
}

template <class T> static void REG_T()
{
	T::ScriptRegisterType(*engine);
}

void script_register(World &world)
{
	engine = &world.hookmanager->engine;

	RegisterScriptMath(engine->as);
	RegisterStdString(engine->as);

	if (world.config["ScriptFileAccess"])
	{
		RegisterScriptFile(engine->as);
	}

	if (world.config["ScriptLibCAccess"])
	{
		RegisterScriptLibC(engine->as);
	}

	// Common things in EOSERV to be exposed to scripts
	REG_T<GenericPtrIterator>();
	REG_T<GenericPtrVector>();
	REG_T<ScriptPtrVector>();
	REG_T<GenericPtrVector>();
	REG_T<GenericPtrList>();
	REG_T<ScriptPtrList>();
	REG_T<GenericPtrList>();

	REG<GenericPtrIterator>();
	REG<GenericPtrVector>();
	REG<ScriptPtrVector>();
	REG<GenericPtrVector>();
	REG<GenericPtrList>();
	REG<ScriptPtrList>();
	REG<GenericPtrList>();

	Console::ScriptRegister(*engine);
	eoconst::ScriptRegister(*engine);

	// Any new script-visible classes must be added here
	REG_T<Arena>();
	REG_T<Arena_Spawn>();
	REG_T<Board_Post>();
	REG_T<Board>();
	REG_T<Config>();
	REG_T<Character_Item>();
	REG_T<Character_Spell>();
	REG_T<Character>();
	REG_T<Database>();
	REG_T<EOServer>();
	REG_T<EIF>();
	REG_T<ENF>();
	REG_T<ESF>();
	REG_T<ECF>();
	REG_T<Guild>();
	REG_T<HookManager>();
	REG_T<IPAddress>();
	REG_T<Map_Chest>();
	REG_T<Map_Item>();
	REG_T<Map_Tile>();
	REG_T<Map_Warp>();
	REG_T<Map>();
	REG_T<NPC>();
	REG_T<Party>();
	REG_T<Player>();
	REG_T<Timer>();
	REG_T<TimeEvent>();
	REG_T<World>();

	// Any new script-visible classes must be added here (yes, again)
	REG<Arena>();
	REG<Arena_Spawn>();
	REG<Board_Post>();
	REG<Board>();
	REG<Config>();
	REG<Character_Item>();
	REG<Character_Spell>();
	REG<Character>();
	REG<Database>();
	REG<EOServer>();
	REG<EIF>();
	REG<ENF>();
	REG<ESF>();
	REG<ECF>();
	REG<Guild>();
	REG<HookManager>();
	REG<IPAddress>();
	REG<Map_Chest>();
	REG<Map_Item>();
	REG<Map_Tile>();
	REG<Map_Warp>();
	REG<Map>();
	REG<NPC>();
	REG<Party>();
	REG<Player>();
	REG<Timer>();
	REG<TimeEvent>();
	REG<World>();
}