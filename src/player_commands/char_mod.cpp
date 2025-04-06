/* player_commands/char_mod.cpp
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "player_commands.hpp"

#include "../command_source.hpp"
#include "../config.hpp"
#include "../i18n.hpp"
#include "../world.hpp"
#include "../npc.hpp"
#include "../packet.hpp"

#include "../console.hpp"
#include "../util.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace PlayerCommands
{

	void AutoLoot(const std::vector<std::string> &arguments, Character *from)
	{
		if (arguments.size())
		{
			// Enable or disable autoloot
			std::string command = util::lowercase(arguments[0]);

			if (command == "on")
			{
				from->autoloot_enabled = true; // Enable autoloot for this player
				from->ServerMsg("Autoloot enabled.");
			}
			else if (command == "off")
			{
				from->autoloot_enabled = false; // Disable autoloot for this player
				from->ServerMsg("Autoloot disabled.");
			}
			else
			{
				from->ServerMsg("Invalid command. Use 'on' or 'off'.");
			}
		}
		else
		{
			// Show current autoloot status
			std::string status = from->autoloot_enabled ? "enabled" : "disabled";
			from->ServerMsg("Autoloot is currently " + status + ".");
		}
	}

	void AutoPotion(const std::vector<std::string> &arguments, Character *from)
	{
		if (arguments.size())
		{
			// Enable or disable autopotion
			std::string command = util::lowercase(arguments[0]);

			if (command == "on")
			{
				from->auto_potion_enabled = true; // Enable auto-potion for this player
				from->ServerMsg("Auto-potion enabled.");
			}
			else if (command == "off")
			{
				from->auto_potion_enabled = false; // Disable auto-potion for this player
				from->ServerMsg("Auto-potion disabled.");
			}
			else
			{
				from->ServerMsg("Invalid command. Use 'on' or 'off'.");
			}
		}
		else
		{
			// Show current auto-potion status
			std::string status = from->auto_potion_enabled ? "enabled" : "disabled";
			from->ServerMsg("Auto-potion is currently " + status + ".");
		}
	}

	void Command_Pet(const std::vector<std::string> &arguments, Character *character)
	{
		if (!character->PetNPC || !character->PetNPC->pet)
		{
			character->ServerMsg("You do not have a pet.");
			return;
		}

		if (arguments.empty())
		{
			character->ServerMsg("Usage: #pet <mode>");
			character->ServerMsg("Modes: attack, guard, follow");
			return;
		}

		std::string mode = util::lowercase(arguments[0]);

		if (mode == "attack")
		{
			if (character->PetNPC) // Ensure PetNPC is valid
			{
				character->PetSetMode("attacking");
				character->ServerMsg("Your pet is now in attacking mode.");
			}
		}
		else if (mode == "guard")
		{
			if (character->PetNPC) // Ensure PetNPC is valid
			{
				character->PetSetMode("guarding");
				character->ServerMsg("Your pet is now in guarding mode.");
			}
		}
		else if (mode == "follow")
		{
			if (character->PetNPC) // Ensure PetNPC is valid
			{
				character->PetSetMode("following");
				character->ServerMsg("Your pet is now in following mode.");
			}
		}
		else
		{
			character->ServerMsg("Invalid mode. Modes: attack, guard, follow");
		}

		// Validate the pet object before sending updates
		if (character->PetNPC && character->PetNPC->map)
		{
			PacketBuilder builder(PACKET_NPC, PACKET_SPEC, 6);
			builder.AddChar(character->PetNPC->index);
			builder.AddChar(mode == "attack" ? 1 : mode == "guard" ? 2
											   : mode == "follow"  ? 3
																   : 0); // 1 = attack, 2 = guard, 3 = follow, 0 = invalid
			builder.AddShort(character->PetNPC->id);
			builder.AddChar(0); // Optional data
			UTIL_FOREACH(character->map->characters, nearby_character)
			{
				if (nearby_character->InRange(character->PetNPC))
				{
					nearby_character->Send(builder);
				}
			}
		}
	}

	PLAYER_COMMAND_HANDLER_REGISTER(char_mod)
	RegisterCharacter({"autoloot", {}, {}}, AutoLoot);
	RegisterCharacter({"autopotion", {}, {}}, AutoPotion);
	RegisterCharacter({"pet", {}, {}}, Command_Pet);

	RegisterAlias("loot", "autoloot");
	RegisterAlias("ap", "autopotion");
	PLAYER_COMMAND_HANDLER_REGISTER_END(char_mod)

}
