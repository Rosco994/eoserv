/* player_commands/char_mod.cpp
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "player_commands.hpp"

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

	PLAYER_COMMAND_HANDLER_REGISTER(char_mod)
	RegisterCharacter({"autoloot", {}, {}}, AutoLoot);

	RegisterAlias("loot", "autoloot");
	PLAYER_COMMAND_HANDLER_REGISTER_END(char_mod)

}
