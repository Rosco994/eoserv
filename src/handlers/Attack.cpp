/* handlers/Attack.cpp
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../map.hpp"
#include "../packet.hpp"
#include "../world.hpp"

namespace Handlers
{

	// Player attacking
	void Attack_Use(Character *character, PacketReader &reader)
	{
		Direction direction = static_cast<Direction>(reader.GetChar());
		Timestamp timestamp = reader.GetThree();

		int ts_diff = timestamp - character->timestamp;

		if (character->world->config["EnforceTimestamps"])
		{
			if (ts_diff < 48)
			{
				return;
			}
		}

		character->timestamp = timestamp;

		if (character->sitting != SIT_STAND)
			return;

		if (int(character->world->config["EnforceWeight"]) >= 1 && character->weight > character->maxweight)
			return;

		int limit_attack = character->world->config["LimitAttack"];

		if (limit_attack != 0 && character->attacks >= limit_attack)
			return;

		if (!character->world->config["EnforceTimestamps"] || ts_diff >= 60)
		{
			direction = character->direction;
		}

		character->Attack(direction);
		character->AutoPotion();
	}

	PACKET_HANDLER_REGISTER(PACKET_ATTACK)
	Register(PACKET_USE, Attack_Use, Playing, 0.25);
	PACKET_HANDLER_REGISTER_END(PACKET_ATTACK)

}
