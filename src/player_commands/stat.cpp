/* player_commands/stat.cpp
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "player_commands.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../dialog.hpp"
#include "../eodata.hpp"
#include "../i18n.hpp"
#include "../packet.hpp"
#include "../world.hpp"

#include "../util.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace PlayerCommands
{

	void Add(const std::vector<std::string> &arguments, Character *from)
	{
		std::string stat = arguments[0];
		int amount = util::to_int(arguments[1]);

		if (amount <= 0 || amount > from->statpoints)
		{
			return;
		}

		if (stat == "str")
		{
			from->str += amount;
		}
		else if (stat == "int")
		{
			from->intl += amount;
		}
		else if (stat == "wis")
		{
			from->wis += amount;
		}
		else if (stat == "agi")
		{
			from->agi += amount;
		}
		else if (stat == "con")
		{
			from->con += amount;
		}
		else if (stat == "cha")
		{
			from->cha += amount;
		}
		else
		{
			from->StatusMsg(from->SourceWorld()->i18n.Format("invalid_stat_name"));
			return;
		}

		from->statpoints -= amount;

		from->CalculateStats();

		PacketBuilder builder(PACKET_STATSKILL, PACKET_PLAYER, 32);
		builder.AddShort(from->statpoints);
		builder.AddShort(from->display_str);
		builder.AddShort(from->display_intl);
		builder.AddShort(from->display_wis);
		builder.AddShort(from->display_agi);
		builder.AddShort(from->display_con);
		builder.AddShort(from->display_cha);
		builder.AddShort(from->maxhp);
		builder.AddShort(from->maxtp);
		builder.AddShort(from->maxsp);
		builder.AddShort(from->maxweight);
		builder.AddShort(from->mindam);
		builder.AddShort(from->maxdam);
		builder.AddShort(from->accuracy);
		builder.AddShort(from->evade);
		builder.AddShort(from->armor);
		from->Send(builder);
	}

	// Reset stat points
	void Reset(const std::vector<std::string> &arguments, Character *from)
	{
		// Call reset func from character
		from->Reset();

		// Send skill master reset packet
		// Taken from StatSkill.cpp StatSkill_Junk()
		PacketBuilder builder(PACKET_STATSKILL, PACKET_JUNK, 36);
		builder.AddShort(from->statpoints);
		builder.AddShort(from->skillpoints);
		builder.AddShort(from->hp);
		builder.AddShort(from->maxhp);
		builder.AddShort(from->tp);
		builder.AddShort(from->maxtp);
		builder.AddShort(from->maxsp);
		builder.AddShort(from->display_str);
		builder.AddShort(from->display_intl);
		builder.AddShort(from->display_wis);
		builder.AddShort(from->display_agi);
		builder.AddShort(from->display_con);
		builder.AddShort(from->display_cha);
		builder.AddShort(from->mindam);
		builder.AddShort(from->maxdam);
		builder.AddShort(from->accuracy);
		builder.AddShort(from->evade);
		builder.AddShort(from->armor);

		from->Send(builder);
		for (Character_Spell spell : from->spells)
		{
			PacketBuilder reply(PACKET_STATSKILL, PACKET_TAKE, 6);
			reply.AddShort(spell.id);
			reply.AddInt(from->HasItem(1));
			from->Send(reply);
		}
		from->ServerMsg("Your stats have been reset");
	}

	PLAYER_COMMAND_HANDLER_REGISTER(info)
	RegisterCharacter({"add", {"stat", "amount"}, {}, 6}, Add);
	RegisterCharacter({"reset", {}, {}, 6}, Reset);
	PLAYER_COMMAND_HANDLER_REGISTER_END(info)

}
