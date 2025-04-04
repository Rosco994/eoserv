/* world.cpp
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "world.hpp"

#include "character.hpp"
#include "command_source.hpp"
#include "config.hpp"
#include "database.hpp"
#include "eoclient.hpp"
#include "eodata.hpp"
#include "eoplus.hpp"
#include "eoserver.hpp"
#include "guild.hpp"
#include "i18n.hpp"
#include "map.hpp"
#include "npc.hpp"
#include "npc_data.hpp"
#include "packet.hpp"
#include "party.hpp"
#include "player.hpp"
#include "quest.hpp"
#include "timer.hpp"
#include "commands/commands.hpp"
#include "player_commands/player_commands.hpp"
#include "handlers/handlers.hpp"

#include "console.hpp"
#include "hash.hpp"
#include "util.hpp"
#include "util/rpn.hpp"
#include "util/secure_string.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <limits>
#include <list>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

void world_spawn_npcs(void *world_void)
{
	World *world(static_cast<World *>(world_void));

	double spawnrate = world->config["SpawnRate"];
	double current_time = Timer::GetTime();
	UTIL_FOREACH(world->maps, map)
	{
		UTIL_FOREACH(map->npcs, npc)
		{
			if ((!npc->alive && npc->dead_since + (double(npc->spawn_time) * spawnrate) < current_time) && (!npc->ENF().child || (npc->parent && npc->parent->alive && world->config["RespawnBossChildren"])))
			{
#ifdef DEBUG
				Console::Dbg("Spawning NPC %i on map %i", npc->id, map->id);
#endif // DEBUG
				npc->Spawn();
			}
		}
	}
}

void world_act_npcs(void *world_void)
{
	World *world(static_cast<World *>(world_void));

	double current_time = Timer::GetTime();
	UTIL_FOREACH(world->maps, map)
	{
		UTIL_FOREACH(map->npcs, npc)
		{
			if (npc->alive && npc->last_act + npc->act_speed < current_time)
			{
				npc->Act();
			}
		}
	}
}

void world_recover(void *world_void)
{
	World *world(static_cast<World *>(world_void));

	UTIL_FOREACH(world->characters, character)
	{
		bool updated = false;

		if (character->hp != character->maxhp)
		{
			if (character->sitting != SIT_STAND)
				character->hp += character->maxhp * double(world->config["SitHPRecoverRate"]);
			else
				character->hp += character->maxhp * double(world->config["HPRecoverRate"]);

			character->hp = std::min(character->hp, character->maxhp);
			updated = true;

			if (character->party)
			{
				character->party->UpdateHP(character);
			}
		}

		if (character->tp != character->maxtp)
		{
			if (character->sitting != SIT_STAND)
				character->tp += character->maxtp * double(world->config["SitTPRecoverRate"]);
			else
				character->tp += character->maxtp * double(world->config["TPRecoverRate"]);

			character->tp = std::min(character->tp, character->maxtp);
			updated = true;
		}

		if (updated)
		{
			PacketBuilder builder(PACKET_RECOVER, PACKET_PLAYER, 6);
			builder.AddShort(character->hp);
			builder.AddShort(character->tp);
			builder.AddShort(0); // ?
			character->Send(builder);
		}
	}
}

void world_npc_recover(void *world_void)
{
	World *world(static_cast<World *>(world_void));

	UTIL_FOREACH(world->maps, map)
	{
		UTIL_FOREACH(map->npcs, npc)
		{
			if (npc->alive && npc->hp < npc->ENF().hp)
			{
				npc->hp += npc->ENF().hp * double(world->config["NPCRecoverRate"]);

				npc->hp = std::min(npc->hp, npc->ENF().hp);
			}
		}
	}
}

void world_warp_suck(void *world_void)
{
	struct Warp_Suck_Action
	{
		Character *character;
		short map;
		unsigned char x;
		unsigned char y;

		Warp_Suck_Action(Character *character, short map, unsigned char x, unsigned char y)
			: character(character), map(map), x(x), y(y)
		{
		}
	};

	std::vector<Warp_Suck_Action> actions;

	World *world(static_cast<World *>(world_void));

	double now = Timer::GetTime();
	double delay = world->config["WarpSuck"];

	UTIL_FOREACH(world->maps, map)
	{
		UTIL_FOREACH(map->characters, character)
		{
			if (character->last_walk + delay >= now)
				continue;

			auto check_warp = [&](bool test, unsigned char x, unsigned char y)
			{
				if (!test || !map->InBounds(x, y))
					return;

				const Map_Warp &warp = map->GetWarp(x, y);

				if (!warp || warp.levelreq > character->level || (warp.spec != Map_Warp::Door && warp.spec != Map_Warp::NoDoor))
					return;

				actions.push_back({character, warp.map, warp.x, warp.y});
			};

			character->last_walk = now;

			check_warp(true, character->x, character->y);
			check_warp(character->x > 0, character->x - 1, character->y);
			check_warp(character->x < map->width, character->x + 1, character->y);
			check_warp(character->y > 0, character->x, character->y - 1);
			check_warp(character->y < map->height, character->x, character->y + 1);
		}
	}

	UTIL_FOREACH(actions, act)
	{
		if (act.character->SourceAccess() < ADMIN_GUIDE && world->GetMap(act.map)->evacuate_lock)
		{
			act.character->StatusMsg(world->i18n.Format("map_evacuate_block"));
			act.character->Refresh();
		}
		else
		{
			act.character->Warp(act.map, act.x, act.y);
		}
	}
}

void world_despawn_items(void *world_void)
{
	World *world = static_cast<World *>(world_void);

	UTIL_FOREACH(world->maps, map)
	{
	restart_loop:
		UTIL_FOREACH(map->items, item)
		{
			if (item->unprotecttime < (Timer::GetTime() - static_cast<double>(world->config["ItemDespawnRate"])))
			{
				map->DelItem(item->uid, 0);
				goto restart_loop;
			}
		}
	}
}

void world_timed_save(void *world_void)
{
	World *world = static_cast<World *>(world_void);

	if (!world->config["TimedSave"])
		return;

	UTIL_FOREACH(world->characters, character)
	{
		character->Save();
	}

	world->guildmanager->SaveAll();

	try
	{
		world->CommitDB();
	}
	catch (Database_Exception &e)
	{
		Console::Wrn("Database commit failed - no data was saved!");
		world->db.Rollback();
	}

	world->BeginDB();
}

void world_spikes(void *world_void)
{
	World *world = static_cast<World *>(world_void);

	for (Map *map : world->maps)
	{
		if (map->exists)
			map->TimedSpikes();
	}
}

void world_drains(void *world_void)
{
	World *world = static_cast<World *>(world_void);

	for (Map *map : world->maps)
	{
		if (map->exists)
			map->TimedDrains();
	}
}

void world_quakes(void *world_void)
{
	World *world = static_cast<World *>(world_void);

	for (Map *map : world->maps)
	{
		if (map->exists)
			map->TimedQuakes();
	}
}

void World::UpdateConfig()
{
	this->timer.SetMaxDelta(this->config["ClockMaxDelta"]);

	double rate_face = this->config["PacketRateFace"];
	double rate_walk = this->config["PacketRateWalk"];
	double rate_attack = this->config["PacketRateAttack"];

	Handlers::SetDelay(PACKET_FACE, PACKET_PLAYER, rate_face);

	Handlers::SetDelay(PACKET_WALK, PACKET_ADMIN, rate_walk);
	Handlers::SetDelay(PACKET_WALK, PACKET_PLAYER, rate_walk);
	Handlers::SetDelay(PACKET_WALK, PACKET_SPEC, rate_walk);

	Handlers::SetDelay(PACKET_ATTACK, PACKET_USE, rate_attack);

	std::array<double, 7> npc_speed_table;

	std::vector<std::string> rate_list = util::explode(',', this->config["NPCMovementRate"]);

	for (std::size_t i = 0; i < std::min<std::size_t>(7, rate_list.size()); ++i)
	{
		if (i < rate_list.size())
			npc_speed_table[i] = util::tdparse(rate_list[i]);
		else
			npc_speed_table[i] = 1.0;
	}

	NPC::SetSpeedTable(npc_speed_table);

	this->i18n.SetLangFile(this->config["ServerLanguage"]);

	this->instrument_ids.clear();

	std::vector<std::string> instrument_list = util::explode(',', this->config["InstrumentItems"]);
	this->instrument_ids.reserve(instrument_list.size());

	for (std::size_t i = 0; i < instrument_list.size(); ++i)
	{
		this->instrument_ids.push_back(int(util::tdparse(instrument_list[i])));
	}

	if (this->db.Pending() && !this->config["TimedSave"])
	{
		try
		{
			this->CommitDB();
		}
		catch (Database_Exception &e)
		{
			Console::Wrn("Database commit failed - no data was saved!");
			this->db.Rollback();
		}
	}
}

World::World(std::array<std::string, 6> dbinfo, const Config &eoserv_config, const Config &admin_config)
	: i18n(eoserv_config.find("ServerLanguage")->second), admin_count(0)
{
	if (int(this->timer.resolution * 1000.0) > 1)
	{
		Console::Out("Timers set at approx. %i ms resolution", int(this->timer.resolution * 1000.0));
	}
	else
	{
		Console::Out("Timers set at < 1 ms resolution");
	}

	this->config = eoserv_config;
	this->admin_config = admin_config;

	Database::Engine engine;

	std::string dbdesc;

	if (util::lowercase(dbinfo[0]).compare("sqlite") == 0)
	{
		engine = Database::SQLite;
		dbdesc = std::string("SQLite: ") + dbinfo[1];
	}
	else
	{
		engine = Database::MySQL;
		dbdesc = std::string("MySQL: ") + dbinfo[2] + "@" + dbinfo[1];

		if (dbinfo[5] != "0" && dbinfo[5] != "3306")
			dbdesc += ":" + dbinfo[5];

		dbdesc += "/" + dbinfo[4];
	}

	Console::Out("Connecting to database (%s)...", dbdesc.c_str());
	this->db.Connect(engine, dbinfo[1], util::to_int(dbinfo[5]), dbinfo[2], dbinfo[3], dbinfo[4]);
	this->BeginDB();

	this->drops_config.Read(this->config["DropsFile"]);
	this->shops_config.Read(this->config["ShopsFile"]);
	this->arenas_config.Read(this->config["ArenasFile"]);
	this->formulas_config.Read(this->config["FormulasFile"]);
	this->home_config.Read(this->config["HomeFile"]);
	this->skills_config.Read(this->config["SkillsFile"]);

	this->UpdateConfig();
	this->LoadHome();

	bool auto_split = this->config["AutoSplitPubFiles"];

	this->eif = new EIF(this->config["EIF"], auto_split);
	this->enf = new ENF(this->config["ENF"], auto_split);
	this->esf = new ESF(this->config["ESF"], auto_split);
	this->ecf = new ECF(this->config["ECF"], auto_split);

	std::size_t num_npcs = this->enf->data.size();
	this->npc_data.resize(num_npcs);
	for (std::size_t i = 0; i < num_npcs; ++i)
	{
		auto &npc = this->npc_data[i];
		npc.reset(new NPC_Data(this, i));
		if (npc->id != 0)
			npc->LoadShopDrop();
	}

	this->maps.resize(static_cast<int>(this->config["Maps"]));
	int loaded = 0;
	for (int i = 0; i < static_cast<int>(this->config["Maps"]); ++i)
	{
		this->maps[i] = new Map(i + 1, this);
		if (this->maps[i]->exists)
			++loaded;
	}
	Console::Out("%i/%i maps loaded.", loaded, static_cast<int>(this->maps.size()));

	short max_quest = static_cast<int>(this->config["Quests"]);

	UTIL_FOREACH(this->enf->data, npc)
	{
		if (npc.type == ENF::Quest)
			max_quest = std::max(max_quest, npc.vendor_id);
	}

	for (short i = 0; i <= max_quest; ++i)
	{
		try
		{
			std::shared_ptr<Quest> q = std::make_shared<Quest>(i, this);
			this->quests.insert(std::make_pair(i, std::move(q)));
		}
		catch (...)
		{
		}
	}
	Console::Out("%i/%i quests loaded.", static_cast<int>(this->quests.size()), max_quest);

	this->last_character_id = 0;

	TimeEvent *event = new TimeEvent(world_spawn_npcs, this, 1.0, Timer::FOREVER);
	this->timer.Register(event);

	event = new TimeEvent(world_act_npcs, this, 0.05, Timer::FOREVER);
	this->timer.Register(event);

	if (int(this->config["RecoverSpeed"]) > 0)
	{
		event = new TimeEvent(world_recover, this, double(this->config["RecoverSpeed"]), Timer::FOREVER);
		this->timer.Register(event);
	}

	if (int(this->config["NPCRecoverSpeed"]) > 0)
	{
		event = new TimeEvent(world_npc_recover, this, double(this->config["NPCRecoverSpeed"]), Timer::FOREVER);
		this->timer.Register(event);
	}

	if (int(this->config["WarpSuck"]) > 0)
	{
		event = new TimeEvent(world_warp_suck, this, 1.0, Timer::FOREVER);
		this->timer.Register(event);
	}

	if (this->config["ItemDespawn"])
	{
		event = new TimeEvent(world_despawn_items, this, static_cast<double>(this->config["ItemDespawnCheck"]), Timer::FOREVER);
		this->timer.Register(event);
	}

	if (this->config["TimedSave"])
	{
		event = new TimeEvent(world_timed_save, this, static_cast<double>(this->config["TimedSave"]), Timer::FOREVER);
		this->timer.Register(event);
	}

	if (this->config["SpikeTime"])
	{
		event = new TimeEvent(world_spikes, this, static_cast<double>(this->config["SpikeTime"]), Timer::FOREVER);
		this->timer.Register(event);
	}

	if (this->config["DrainTime"])
	{
		event = new TimeEvent(world_drains, this, static_cast<double>(this->config["DrainTime"]), Timer::FOREVER);
		this->timer.Register(event);
	}

	if (this->config["QuakeRate"])
	{
		event = new TimeEvent(world_quakes, this, static_cast<double>(this->config["QuakeRate"]), Timer::FOREVER);
		this->timer.Register(event);
	}

	exp_table[0] = 0;
	for (std::size_t i = 1; i < this->exp_table.size(); ++i)
	{
		exp_table[i] = int(util::round(std::pow(double(i), 3.0) * 133.1));
	}

	for (std::size_t i = 0; i < this->boards.size(); ++i)
	{
		this->boards[i] = new Board(i);
	}

	this->guildmanager = new GuildManager(this);
}

void World::BeginDB()
{
	if (this->config["TimedSave"])
		this->db.BeginTransaction();
}

void World::CommitDB()
{
	if (this->db.Pending())
		this->db.Commit();
}

void World::UpdateAdminCount(int admin_count)
{
	this->admin_count = admin_count;

	if (admin_count == 0 && this->config["FirstCharacterAdmin"])
	{
		Console::Out("There are no admin characters!");
		Console::Out("The next character created will be given HGM status!");
	}
}

void World::Command(std::string command, const std::vector<std::string> &arguments, Command_Source *from)
{
	std::unique_ptr<System_Command_Source> system_source;

	if (!from)
	{
		system_source.reset(new System_Command_Source(this));
		from = system_source.get();
	}

	Commands::Handle(util::lowercase(command), arguments, from);
}

void World::PlayerCommand(std::string command, const std::vector<std::string> &arguments, Command_Source *from)
{
	std::unique_ptr<System_Command_Source> system_source;

	if (!from)
	{
		system_source.reset(new System_Command_Source(this));
		from = system_source.get();
	}

	PlayerCommands::Handle(util::lowercase(command), arguments, from);
}

void World::LoadHome()
{
	this->homes.clear();

	std::unordered_map<std::string, Home *> temp_homes;

	UTIL_FOREACH(this->home_config, hc)
	{
		std::vector<std::string> parts = util::explode('.', hc.first);

		if (parts.size() < 2)
		{
			continue;
		}

		if (parts[0] == "level")
		{
			int level = util::to_int(parts[1]);

			std::unordered_map<std::string, Home *>::iterator home_iter = temp_homes.find(hc.second);

			if (home_iter == temp_homes.end())
			{
				Home *home = new Home;
				home->id = static_cast<std::string>(hc.second);
				temp_homes[hc.second] = home;
				home->level = level;
			}
			else
			{
				home_iter->second->level = level;
			}

			continue;
		}

		Home *&home = temp_homes[parts[0]];

		if (!home)
		{
			temp_homes[parts[0]] = home = new Home;
			home->id = parts[0];
		}

		if (parts[1] == "name")
		{
			home->name = home->name = static_cast<std::string>(hc.second);
		}
		else if (parts[1] == "location")
		{
			std::vector<std::string> locparts = util::explode(',', hc.second);
			home->map = locparts.size() >= 1 ? util::to_int(locparts[0]) : 1;
			home->x = locparts.size() >= 2 ? util::to_int(locparts[1]) : 0;
			home->y = locparts.size() >= 3 ? util::to_int(locparts[2]) : 0;
		}
		else if (parts[1] == "sleep")
		{
			std::vector<std::string> locparts = util::explode(',', hc.second);
			home->sleep_map = locparts.size() >= 1 ? util::to_int(locparts[0]) : 0;
			home->sleep_x = locparts.size() >= 2 ? util::to_int(locparts[1]) : 0;
			home->sleep_y = locparts.size() >= 3 ? util::to_int(locparts[2]) : 0;
		}
	}

	UTIL_FOREACH(temp_homes, home)
	{
		this->homes.push_back(home.second);
	}
}

int World::GenerateCharacterID()
{
	return ++this->last_character_id;
}

int World::GeneratePlayerID()
{
	unsigned int lowest_free_id = 1;
restart_loop:
	UTIL_FOREACH(this->server->clients, client)
	{
		EOClient *eoclient = static_cast<EOClient *>(client);

		if (eoclient->id == lowest_free_id)
		{
			lowest_free_id = eoclient->id + 1;
			goto restart_loop;
		}
	}
	return lowest_free_id;
}

void World::Login(Character *character)
{
	this->characters.push_back(character);

	if (this->GetMap(character->mapid)->relog_x || this->GetMap(character->mapid)->relog_y)
	{
		character->x = this->GetMap(character->mapid)->relog_x;
		character->y = this->GetMap(character->mapid)->relog_y;
	}

	Map *map = this->GetMap(character->mapid);

	if (character->sitting == SIT_CHAIR)
	{
		Map_Tile::TileSpec spec = map->GetSpec(character->x, character->y);

		if (spec == Map_Tile::ChairDown)
			character->direction = DIRECTION_DOWN;
		else if (spec == Map_Tile::ChairUp)
			character->direction = DIRECTION_UP;
		else if (spec == Map_Tile::ChairLeft)
			character->direction = DIRECTION_LEFT;
		else if (spec == Map_Tile::ChairRight)
			character->direction = DIRECTION_RIGHT;
		else if (spec == Map_Tile::ChairDownRight)
			character->direction = character->direction == DIRECTION_RIGHT ? DIRECTION_RIGHT : DIRECTION_DOWN;
		else if (spec == Map_Tile::ChairUpLeft)
			character->direction = character->direction == DIRECTION_LEFT ? DIRECTION_LEFT : DIRECTION_UP;
		else if (spec != Map_Tile::ChairAll)
			character->sitting = SIT_STAND;
	}

	map->Enter(character);
	character->Login();
}

void World::Logout(Character *character)
{
	if (this->GetMap(character->mapid)->exists)
	{
		this->GetMap(character->mapid)->Leave(character);
	}

	this->characters.erase(
		std::remove(UTIL_RANGE(this->characters), character),
		this->characters.end());
}

void World::Msg(Command_Source *from, std::string message, bool echo)
{
	std::string from_str = from ? from->SourceName() : "server";

	message = util::text_cap(message, static_cast<int>(this->config["ChatMaxWidth"]) - util::text_width(util::ucfirst(from_str) + "  "));

	PacketBuilder builder(PACKET_TALK, PACKET_MSG, 2 + from_str.length() + message.length());
	builder.AddBreakString(from_str);
	builder.AddBreakString(message);

	UTIL_FOREACH(this->characters, character)
	{
		character->AddChatLog("~", from_str, message);

		if (!echo && character == from)
		{
			continue;
		}

		character->Send(builder);
	}
}

void World::AdminMsg(Command_Source *from, std::string message, int minlevel, bool echo)
{
	std::string from_str = from ? from->SourceName() : "server";

	message = util::text_cap(message, static_cast<int>(this->config["ChatMaxWidth"]) - util::text_width(util::ucfirst(from_str) + "  "));

	PacketBuilder builder(PACKET_TALK, PACKET_ADMIN, 2 + from_str.length() + message.length());
	builder.AddBreakString(from_str);
	builder.AddBreakString(message);

	UTIL_FOREACH(this->characters, character)
	{
		character->AddChatLog("+", from_str, message);

		if ((!echo && character == from) || character->SourceAccess() < minlevel)
		{
			continue;
		}

		character->Send(builder);
	}
}

void World::AnnounceMsg(Command_Source *from, std::string message, bool echo)
{
	std::string from_str = from ? from->SourceName() : "server";

	message = util::text_cap(message, static_cast<int>(this->config["ChatMaxWidth"]) - util::text_width(util::ucfirst(from_str) + "  "));

	PacketBuilder builder(PACKET_TALK, PACKET_ANNOUNCE, 2 + from_str.length() + message.length());
	builder.AddBreakString(from_str);
	builder.AddBreakString(message);

	UTIL_FOREACH(this->characters, character)
	{
		character->AddChatLog("@", from_str, message);

		if (!echo && character == from)
		{
			continue;
		}

		character->Send(builder);
	}
}

void World::ServerMsg(std::string message)
{
	message = util::text_cap(message, static_cast<int>(this->config["ChatMaxWidth"]) - util::text_width("Server  "));

	PacketBuilder builder(PACKET_TALK, PACKET_SERVER, message.length());
	builder.AddString(message);

	UTIL_FOREACH(this->characters, character)
	{
		character->Send(builder);
	}
}

void World::AdminReport(Character *from, std::string reportee, std::string message)
{
	message = util::text_cap(message, static_cast<int>(this->config["ChatMaxWidth"]) - util::text_width(util::ucfirst(from->SourceName()) + "  reports: " + reportee + ", "));

	PacketBuilder builder(PACKET_ADMININTERACT, PACKET_REPLY, 5 + from->SourceName().length() + message.length() + reportee.length());
	builder.AddChar(2); // message type
	builder.AddByte(255);
	builder.AddBreakString(from->SourceName());
	builder.AddBreakString(message);
	builder.AddBreakString(reportee);

	UTIL_FOREACH(this->characters, character)
	{
		if (character->SourceAccess() >= static_cast<int>(this->admin_config["reports"]))
		{
			character->Send(builder);
		}
	}

	short boardid = static_cast<int>(this->config["AdminBoard"]) - 1;

	if (static_cast<std::size_t>(boardid) < this->boards.size())
	{
		std::string chat_log_dump;
		Board *admin_board = this->boards[boardid];

		Board_Post *newpost = new Board_Post;
		newpost->id = ++admin_board->last_id;
		newpost->author = from->SourceName();
		newpost->author_admin = from->admin;
		newpost->subject = std::string(" [Report] ") + util::ucfirst(from->SourceName()) + " reports: " + reportee;
		newpost->body = message;
		newpost->time = Timer::GetTime();

		if (int(this->config["ReportChatLogSize"]) > 0)
		{
			chat_log_dump = from->GetChatLogDump();
			newpost->body += "\r\n\r\n";
			newpost->body += chat_log_dump;
		}

		if (this->config["LogReports"])
		{
			try
			{
				this->db.Query("INSERT INTO `reports` (`reporter`, `reported`, `reason`, `time`, `chat_log`) VALUES ('$', '$', '$', #, '$')",
							   from->SourceName().c_str(),
							   reportee.c_str(),
							   message.c_str(),
							   int(std::time(0)),
							   chat_log_dump.c_str());
			}
			catch (Database_Exception &e)
			{
				Console::Err("Could not save report to database.");
				Console::Err("%s", e.error());
			}
		}

		admin_board->posts.push_front(newpost);

		if (admin_board->posts.size() > static_cast<std::size_t>(static_cast<int>(this->config["AdminBoardLimit"])))
		{
			admin_board->posts.pop_back();
		}
	}
}

void World::AdminRequest(Character *from, std::string message)
{
	message = util::text_cap(message, static_cast<int>(this->config["ChatMaxWidth"]) - util::text_width(util::ucfirst(from->SourceName()) + "  needs help: "));

	PacketBuilder builder(PACKET_ADMININTERACT, PACKET_REPLY, 4 + from->SourceName().length() + message.length());
	builder.AddChar(1); // message type
	builder.AddByte(255);
	builder.AddBreakString(from->SourceName());
	builder.AddBreakString(message);

	UTIL_FOREACH(this->characters, character)
	{
		if (character->SourceAccess() >= static_cast<int>(this->admin_config["reports"]))
		{
			character->Send(builder);
		}
	}

	short boardid = static_cast<int>(this->server->world->config["AdminBoard"]) - 1;

	if (static_cast<std::size_t>(boardid) < this->server->world->boards.size())
	{
		Board *admin_board = this->server->world->boards[boardid];

		Board_Post *newpost = new Board_Post;
		newpost->id = ++admin_board->last_id;
		newpost->author = from->SourceName();
		newpost->author_admin = from->admin;
		newpost->subject = std::string(" [Request] ") + util::ucfirst(from->SourceName()) + " needs help";
		newpost->body = message;
		newpost->time = Timer::GetTime();

		admin_board->posts.push_front(newpost);

		if (admin_board->posts.size() > static_cast<std::size_t>(static_cast<int>(this->server->world->config["AdminBoardLimit"])))
		{
			admin_board->posts.pop_back();
		}
	}
}

void World::Rehash()
{
	this->config.Read("config.ini");
	this->admin_config.Read("admin.ini");
	this->drops_config.Read(this->config["DropsFile"]);
	this->shops_config.Read(this->config["ShopsFile"]);
	this->arenas_config.Read(this->config["ArenasFile"]);
	this->formulas_config.Read(this->config["FormulasFile"]);
	this->home_config.Read(this->config["HomeFile"]);
	this->skills_config.Read(this->config["SkillsFile"]);

	this->formulas_cache.clear();

	this->UpdateConfig();
	this->LoadHome();
	this->server->UpdateConfig();

	UTIL_FOREACH(this->maps, map)
	{
		map->LoadArena();
	}

	UTIL_FOREACH_CREF(this->npc_data, npc)
	{
		if (npc->id != 0)
			npc->LoadShopDrop();
	}
}

void World::ReloadPub(bool quiet)
{
	auto eif_id = this->eif->rid;
	auto enf_id = this->enf->rid;
	auto esf_id = this->esf->rid;
	auto ecf_id = this->ecf->rid;

	bool auto_split = this->config["AutoSplitPubFiles"];

	this->eif->Read(this->config["EIF"], auto_split);
	this->enf->Read(this->config["ENF"], auto_split);
	this->esf->Read(this->config["ESF"], auto_split);
	this->ecf->Read(this->config["ECF"], auto_split);

	if (eif_id != this->eif->rid || enf_id != this->enf->rid || esf_id != this->esf->rid || ecf_id != this->ecf->rid)
	{
		if (!quiet)
		{
			UTIL_FOREACH(this->characters, character)
			{
				character->ServerMsg("The server has been reloaded, please log out and in again.");
			}
		}
	}

	std::size_t current_npcs = this->npc_data.size();
	std::size_t new_npcs = this->enf->data.size();

	this->npc_data.resize(new_npcs);

	for (std::size_t i = current_npcs; i < new_npcs; ++i)
	{
		npc_data[i]->LoadShopDrop();
	}
}

void World::ReloadQuests()
{
	// Back up character quest states
	UTIL_FOREACH(this->characters, c)
	{
		UTIL_FOREACH(c->quests, q)
		{
			if (!q.second)
				continue;

			short quest_id = q.first;
			std::string quest_name = q.second->StateName();
			std::string progress = q.second->SerializeProgress();

			c->quests_inactive.insert({quest_id, quest_name, progress});
		}
	}

	// Clear character quest states
	UTIL_FOREACH(this->characters, c)
	{
		c->quests.clear();
	}

	// Reload all quests
	short max_quest = static_cast<int>(this->config["Quests"]);

	UTIL_FOREACH(this->enf->data, npc)
	{
		if (npc.type == ENF::Quest)
			max_quest = std::max(max_quest, npc.vendor_id);
	}

	for (short i = 0; i <= max_quest; ++i)
	{
		try
		{
			std::shared_ptr<Quest> q = std::make_shared<Quest>(i, this);
			this->quests[i] = std::move(q);
		}
		catch (...)
		{
			this->quests.erase(i);
		}
	}

	// Reload quests that might still be loaded above the highest quest npc ID
	UTIL_IFOREACH(this->quests, it)
	{
		if (it->first > max_quest)
		{
			try
			{
				std::shared_ptr<Quest> q = std::make_shared<Quest>(it->first, this);
				std::swap(it->second, q);
			}
			catch (...)
			{
				it = this->quests.erase(it);
			}
		}
	}

	// Restore character quest states
	UTIL_FOREACH(this->characters, c)
	{
		c->quests.clear();

		UTIL_FOREACH(c->quests_inactive, state)
		{
			auto quest_it = this->quests.find(state.quest_id);

			if (quest_it == this->quests.end())
			{
				Console::Wrn("Quest not found: %i. Marking as inactive.", state.quest_id);
				continue;
			}

			// WARNING: holds a non-tracked reference to shared_ptr
			Quest *quest = quest_it->second.get();
			auto quest_context(std::make_shared<Quest_Context>(c, quest));

			try
			{
				quest_context->SetState(state.quest_state, false);
				quest_context->UnserializeProgress(UTIL_CRANGE(state.quest_progress));
			}
			catch (EOPlus::Runtime_Error &ex)
			{
				Console::Wrn(ex.what());
				Console::Wrn("Could not resume quest: %i. Marking as inactive.", state.quest_id);

				if (!c->quests_inactive.insert(std::move(state)).second)
					Console::Wrn("Duplicate inactive quest record dropped for quest: %i", state.quest_id);

				continue;
			}

			auto result = c->quests.insert(std::make_pair(state.quest_id, std::move(quest_context)));

			if (!result.second)
			{
				Console::Wrn("Duplicate quest record dropped for quest: %i", state.quest_id);
				continue;
			}
		}

		// Handle characters removed by quest rules
		if (!c) // Replaced c->IsValid() with a null check
		{
			this->Logout(c);
			continue;
		}

		c->CheckQuestRules();
	}

	Console::Out("%i/%i quests loaded.", this->quests.size(), max_quest);
}

Character *World::GetCharacter(std::string name)
{
	name = util::lowercase(name);

	UTIL_FOREACH(this->characters, character)
	{
		if (character->SourceName() == name)
		{
			return character;
		}
	}

	return 0;
}

Character *World::GetCharacterReal(std::string real_name)
{
	real_name = util::lowercase(real_name);

	UTIL_FOREACH(this->characters, character)
	{
		if (character->real_name == real_name)
		{
			return character;
		}
	}

	return 0;
}

Character *World::GetCharacterPID(unsigned int id)
{
	UTIL_FOREACH(this->characters, character)
	{
		if (character->PlayerID() == id)
		{
			return character;
		}
	}

	return 0;
}

Character *World::GetCharacterCID(unsigned int id)
{
	UTIL_FOREACH(this->characters, character)
	{
		if (character->id == id)
		{
			return character;
		}
	}

	return 0;
}

Map *World::GetMap(short id)
{
	try
	{
		return this->maps.at(id - 1);
	}
	catch (...)
	{
		try
		{
			return this->maps.at(0);
		}
		catch (...)
		{
			throw std::runtime_error("Map #" + util::to_string(id) + " and fallback map #1 are unavailable");
		}
	}
}

const NPC_Data *World::GetNpcData(short id) const
{
	if (id >= 0 && id < npc_data.size())
		return npc_data[id].get();
	else
		return npc_data[0].get();
}

Home *World::GetHome(const Character *character) const
{
	Home *home = 0;
	static Home *null_home = new Home;

	UTIL_FOREACH(this->homes, h)
	{
		if (h->id == character->home)
		{
			return h;
		}
	}

	int current_home_level = -2;
	UTIL_FOREACH(this->homes, h)
	{
		if (h->level <= character->level && h->level > current_home_level)
		{
			home = h;
			current_home_level = h->level;
		}
	}

	if (!home)
	{
		home = null_home;
	}

	return home;
}

Home *World::GetHome(std::string id)
{
	UTIL_FOREACH(this->homes, h)
	{
		if (h->id == id)
		{
			return h;
		}
	}

	return 0;
}

bool World::CharacterExists(std::string name)
{
	Database_Result res = this->db.Query("SELECT 1 FROM `characters` WHERE `name` = '$'", name.c_str());
	return !res.empty();
}

Character *World::CreateCharacter(Player *player, std::string name, Gender gender, int hairstyle, int haircolor, Skin race)
{
	char buffer[1024];
	std::string startmapinfo;
	std::string startmapval;

	if (static_cast<int>(this->config["StartMap"]))
	{
		using namespace std;
		startmapinfo = ", `map`, `x`, `y`";
		snprintf(buffer, 1024, ",%i,%i,%i", static_cast<int>(this->config["StartMap"]), static_cast<int>(this->config["StartX"]), static_cast<int>(this->config["StartY"]));
		startmapval = buffer;
	}

	this->db.Query("INSERT INTO `characters` (`name`, `account`, `gender`, `hairstyle`, `haircolor`, `race`, `inventory`, `bank`, `paperdoll`, `spells`, `quest`, `vars`@) VALUES ('$','$',#,#,#,#,'$','','$','$','',''@)",
				   startmapinfo.c_str(), name.c_str(), player->username.c_str(), gender, hairstyle, haircolor, race,
				   static_cast<std::string>(this->config["StartItems"]).c_str(), static_cast<std::string>(gender ? this->config["StartEquipMale"] : this->config["StartEquipFemale"]).c_str(),
				   static_cast<std::string>(this->config["StartSpells"]).c_str(), startmapval.c_str());

	return new Character(name, this);
}

void World::DeleteCharacter(std::string name)
{
	this->db.Query("DELETE FROM `characters` WHERE name = '$'", name.c_str());
}

Player *World::Login(const std::string &username, util::secure_string &&password)
{
	if (LoginCheck(username, std::move(password)) == LOGIN_WRONG_USERPASS)
		return 0;

	return new Player(username, this);
}

Player *World::Login(std::string username)
{
	return new Player(username, this);
}

LoginReply World::LoginCheck(const std::string &username, util::secure_string &&password)
{
	{
		util::secure_string password_buffer(std::move(std::string(this->config["PasswordSalt"]) + username + password.str()));
		password = sha256(password_buffer.str());
	}

	Database_Result res = this->db.Query("SELECT 1 FROM `accounts` WHERE `username` = '$' AND `password` = '$'", username.c_str(), password.str().c_str());

	if (res.empty())
	{
		return LOGIN_WRONG_USERPASS;
	}
	else if (this->PlayerOnline(username))
	{
		return LOGIN_LOGGEDIN;
	}
	else
	{
		return LOGIN_OK;
	}
}

bool World::CreatePlayer(const std::string &username, util::secure_string &&password,
						 const std::string &fullname, const std::string &location, const std::string &email,
						 const std::string &computer, const std::string &hdid, const std::string &ip)
{
	{
		util::secure_string password_buffer(std::move(std::string(this->config["PasswordSalt"]) + username + password.str()));
		password = sha256(password_buffer.str());
	}

	Database_Result result = this->db.Query("INSERT INTO `accounts` (`username`, `password`, `fullname`, `location`, `email`, `computer`, `hdid`, `regip`, `created`) VALUES ('$','$','$','$','$','$','$','$',#)",
											username.c_str(), password.str().c_str(), fullname.c_str(), location.c_str(), email.c_str(), computer.c_str(), hdid.c_str(), ip.c_str(), int(std::time(0)));

	return !result.Error();
}

bool World::PlayerExists(std::string username)
{
	Database_Result res = this->db.Query("SELECT 1 FROM `accounts` WHERE `username` = '$'", username.c_str());
	return !res.empty();
}

bool World::PlayerOnline(std::string username)
{
	if (!Player::ValidName(username))
	{
		return false;
	}

	UTIL_FOREACH(this->server->clients, client)
	{
		EOClient *eoclient = static_cast<EOClient *>(client);

		if (eoclient->player)
		{
			if (eoclient->player->username.compare(username) == 0)
			{
				return true;
			}
		}
	}

	return false;
}

void World::Kick(Command_Source *from, Character *victim, bool announce)
{
	if (announce)
		this->ServerMsg(i18n.Format("announce_removed", victim->SourceName(), from ? from->SourceName() : "server", i18n.Format("kicked")));

	victim->player->client->Close();
}

void World::Jail(Command_Source *from, Character *victim, bool announce)
{
	if (announce)
		this->ServerMsg(i18n.Format("announce_removed", victim->SourceName(), from ? from->SourceName() : "server", i18n.Format("jailed")));

	bool bubbles = this->config["WarpBubbles"] && !victim->IsHideWarp();

	Character *charfrom = dynamic_cast<Character *>(from);

	if (charfrom && charfrom->IsHideWarp())
		bubbles = false;

	victim->Warp(static_cast<int>(this->config["JailMap"]), static_cast<int>(this->config["JailX"]), static_cast<int>(this->config["JailY"]), bubbles ? WARP_ANIMATION_ADMIN : WARP_ANIMATION_NONE);
}

void World::Unjail(Command_Source *from, Character *victim)
{
	bool bubbles = this->config["WarpBubbles"] && !victim->IsHideWarp();

	Character *charfrom = dynamic_cast<Character *>(from);

	if (charfrom && charfrom->IsHideWarp())
		bubbles = false;

	if (victim->mapid != static_cast<int>(this->config["JailMap"]))
		return;

	victim->Warp(static_cast<int>(this->config["JailMap"]), static_cast<int>(this->config["UnJailX"]), static_cast<int>(this->config["UnJailY"]), bubbles ? WARP_ANIMATION_ADMIN : WARP_ANIMATION_NONE);
}

void World::Ban(Command_Source *from, Character *victim, int duration, bool announce)
{
	std::string from_str = from ? from->SourceName() : "server";

	if (announce)
		this->ServerMsg(i18n.Format("announce_removed", victim->SourceName(), from_str, i18n.Format("banned")));

	std::string query("INSERT INTO bans (username, ip, hdid, expires, setter) VALUES ");

	query += "('" + db.Escape(victim->player->username) + "', ";
	query += util::to_string(static_cast<int>(victim->player->client->GetRemoteAddr())) + ", ";
	query += util::to_string(victim->player->client->hdid) + ", ";

	if (duration == -1)
	{
		query += "0";
	}
	else
	{
		query += util::to_string(int(std::time(0) + duration));
	}

	query += ", '" + db.Escape(from_str) + "')";

	try
	{
		this->db.Query(query.c_str());
	}
	catch (Database_Exception &e)
	{
		Console::Err("Could not save ban to database.");
		Console::Err("%s", e.error());
	}

	victim->player->client->Close();
}

void World::Mute(Command_Source *from, Character *victim, bool announce)
{
	if (announce && !this->config["SilentMute"])
		this->ServerMsg(i18n.Format("announce_muted", victim->SourceName(), from ? from->SourceName() : "server", i18n.Format("banned")));

	victim->Mute(from);
}

int World::CheckBan(const std::string *username, const IPAddress *address, const int *hdid)
{
	std::string query("SELECT COALESCE(MAX(expires),-1) AS expires FROM bans WHERE (");

	if (!username && !address && !hdid)
	{
		return -1;
	}

	if (username)
	{
		query += "username = '";
		query += db.Escape(*username);
		query += "' OR ";
	}

	if (address)
	{
		query += "ip = ";
		query += util::to_string(static_cast<int>(*const_cast<IPAddress *>(address)));
		query += " OR ";
	}

	if (hdid)
	{
		query += "hdid = ";
		query += util::to_string(*hdid);
		query += " OR ";
	}

	Database_Result res = db.Query((query.substr(0, query.length() - 4) + ") AND (expires > # OR expires = 0)").c_str(), int(std::time(0)));

	return static_cast<int>(res[0]["expires"]);
}

static std::list<int> PKExceptUnserialize(std::string serialized)
{
	std::list<int> list;
	std::size_t p = 0;
	std::size_t lastp = std::numeric_limits<std::size_t>::max();

	if (!serialized.empty() && *(serialized.end() - 1) != ',')
	{
		serialized.push_back(',');
	}

	while ((p = serialized.find_first_of(',', p + 1)) != std::string::npos)
	{
		list.push_back(util::to_int(serialized.substr(lastp + 1, p - lastp - 1)));
		lastp = p;
	}

	return list;
}

bool World::PKExcept(const Map *map)
{
	return this->PKExcept(map->id);
}

bool World::PKExcept(int mapid)
{
	if (mapid == static_cast<int>(this->config["JailMap"]))
	{
		return true;
	}

	if (this->GetMap(mapid)->arena)
	{
		return true;
	}

	std::list<int> except_list = PKExceptUnserialize(this->config["PKExcept"]);

	return std::find(except_list.begin(), except_list.end(), mapid) != except_list.end();
}

bool World::IsInstrument(int graphic_id)
{
	return std::find(UTIL_RANGE(this->instrument_ids), graphic_id) != this->instrument_ids.end();
}

double World::EvalFormula(const std::string &name, const std::unordered_map<std::string, double> &vars)
{
	auto cache_it = this->formulas_cache.find(name);

	if (cache_it != this->formulas_cache.end())
		return util::rpn_eval(cache_it->second, vars);

	std::stack<std::string> (*parser)(std::string expr) = util::rpn_parse_v2;

	if (int(this->formulas_config["Version"]) < 2)
		parser = util::rpn_parse;

	auto result = this->formulas_cache.insert({std::string(name), parser(this->formulas_config[name])});

	return util::rpn_eval(result.first->second, vars);
}

World::~World()
{
	UTIL_FOREACH(this->maps, map)
	{
		delete map;
	}

	UTIL_FOREACH(this->homes, home)
	{
		delete home;
	}

	UTIL_FOREACH(this->boards, board)
	{
		delete board;
	}

	delete this->eif;
	delete this->enf;
	delete this->esf;
	delete this->ecf;

	delete this->guildmanager;

	if (this->config["TimedSave"])
	{
		this->db.Commit();
	}
}
