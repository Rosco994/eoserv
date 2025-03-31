/* player_commands/char_mod.cpp
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "player_commands.hpp"
#include "../npc.hpp"

#include "../command_source.hpp"
#include "../config.hpp"
#include "../i18n.hpp"
#include "../world.hpp"

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
		if (!character->has_pet || !character->pet)
		{
			character->StatusMsg("You don't have a pet.");
			return;
		}

		if (!character->pet->alive)
		{
			character->StatusMsg("Your pet is not alive.");
			return;
		}

		if (arguments.empty())
		{
			character->StatusMsg("Usage: #pet <follow|guard|attack>");
			return;
		}

		const std::string &action = util::lowercase(arguments[0]);

		if (action == "follow")
		{
			character->pet->PetFollow(character);
			character->StatusMsg("Your pet is now following you.");
		}
		else if (action == "guard")
		{
			character->pet->PetGuard();
			character->StatusMsg("Your pet is now guarding you.");
		}
		else if (action == "attack")
		{
			character->pet->PetAttack();
			character->StatusMsg("Your pet is now in attack mode.");
		}
		else
		{
			character->StatusMsg("Invalid action. Usage: #pet <follow|guard|attack>");
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
