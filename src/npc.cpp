/* npc.cpp
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "npc.hpp"

#include "character.hpp"
#include "config.hpp"
#include "eodata.hpp"
#include "map.hpp"
#include "npc_data.hpp"
#include "packet.hpp"
#include "party.hpp"
#include "quest.hpp"
#include "timer.hpp"
#include "world.hpp"

#include "console.hpp"
#include "util.hpp"
#include "util/rpn.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <random>
#include <queue> // For A* pathfinding
#include <unordered_set>

static double speed_table[8] = {0.9, 0.6, 1.3, 1.9, 3.7, 7.5, 15.0, 0.0};

void NPC::SetSpeedTable(std::array<double, 7> speeds)
{
	for (std::size_t i = 0; i < 7; ++i)
	{
		if (speeds[i] != 0.0)
		{
			speed_table[i] = speeds[i];
		}
	}
}

NPC::NPC(Map *map, short id, unsigned char x, unsigned char y, unsigned char spawn_type, short spawn_time, unsigned char index, bool temporary, bool pet, Character *PetOwner)
	: map(map), id(id), x(x), y(y), spawn_type(spawn_type), spawn_time(spawn_time), index(index), temporary(temporary), pet(pet), PetOwner(PetOwner)
{
	this->id = id;
	this->map = map;
	this->temporary = temporary;
	this->index = index;
	this->spawn_x = this->x = x;
	this->spawn_y = this->y = y;
	this->alive = false;
	this->attack = false;
	this->totaldamage = 0;

	if (spawn_type > 7)
	{
		spawn_type = 7;
	}

	this->spawn_type = spawn_type;
	this->spawn_time = spawn_time;
	this->walk_idle_for = 0;

	if (spawn_type == 7)
	{
		this->direction = static_cast<Direction>(spawn_time & 0x03);
		this->spawn_time = 0;
	}
	else
	{
		this->direction = DIRECTION_DOWN;
	}

	this->parent = 0;
	this->PetMinDamage = this->ENF().mindam;
	this->PetMaxDamage = this->ENF().maxdam;
	this->PetFollowing = true;
	this->PetAttacking = false;
	this->PetGuarding = false;
	this->PetTarget = nullptr;
	this->direction_changed = false;
}

const NPC_Data &NPC::Data() const
{
	return *this->map->world->GetNpcData(this->id);
}

const ENF_Data &NPC::ENF() const
{
	return this->Data().ENF();
}

void NPC::Spawn(NPC *parent)
{
	if (this->alive)
		return;

	if (this->ENF().boss && !parent)
	{
		UTIL_FOREACH(this->map->npcs, npc)
		{
			if (npc->ENF().child)
			{
				npc->Spawn(this);
			}
		}
	}

	if (parent)
	{
		this->parent = parent;
	}

	if (this->spawn_type < 7)
	{
		bool found = false;
		for (int i = 0; i < 200; ++i)
		{
			if (this->temporary && i == 0)
			{
				this->x = this->spawn_x;
				this->y = this->spawn_y;
			}
			else
			{
				this->x = util::rand(this->spawn_x - 2, this->spawn_x + 2);
				this->y = util::rand(this->spawn_y - 2, this->spawn_y + 2);
			}

			if (this->map->Walkable(this->x, this->y, true) && (i > 100 || !this->map->Occupied(this->x, this->y, Map::NPCOnly)))
			{
				this->direction = static_cast<Direction>(util::rand(0, 3));
				found = true;
				break;
			}
		}

		if (!found)
		{
			Console::Wrn("An NPC on map %i at %i,%i is being placed by linear scan of spawn area (%s)", this->map->id, this->spawn_x, this->spawn_y, this->map->world->enf->Get(this->id).name.c_str());
			for (this->x = this->spawn_x - 2; this->x <= spawn_x + 2; ++this->x)
			{
				for (this->y = this->spawn_y - 2; this->y <= this->spawn_y + 2; ++this->y)
				{
					if (this->map->Walkable(this->x, this->y, true))
					{
						Console::Wrn("Placed at valid location: %i,%i", this->x, this->y);
						found = true;
						goto end_linear_scan;
					}
				}
			}
		}
	end_linear_scan:

		if (!found)
		{
			Console::Err("NPC couldn't spawn anywhere valid!");
		}
	}

	this->alive = true;
	this->hp = this->ENF().hp;
	this->last_act = Timer::GetTime();
	this->act_speed = speed_table[this->spawn_type];

	PacketBuilder builder(PACKET_RANGE, PACKET_REPLY, 8);
	builder.AddChar(0);
	builder.AddByte(255);
	builder.AddChar(this->index);
	builder.AddShort(this->id);
	builder.AddChar(this->x);
	builder.AddChar(this->y);
	builder.AddChar(this->direction);

	UTIL_FOREACH(this->map->characters, character)
	{
		if (character->InRange(this))
		{
			character->Send(builder);
		}
	}
}

void NPC::Act()
{
	// Needed for the server startup spawn to work properly
	if (this->ENF().child && !this->parent)
	{
		UTIL_FOREACH(this->map->npcs, npc)
		{
			if (npc->ENF().boss)
			{
				this->parent = npc;
				break;
			}
		}
	}

	this->last_act += double(util::rand(int(this->act_speed * 750.0), int(this->act_speed * 1250.0))) / 1000.0;

	if (this->spawn_type == 7)
	{
		return;
	}

	// Cache frequently accessed configuration values
	const auto &config = this->map->world->config;
	int max_pets = static_cast<int>(config.at("MaxPets"));
	int respawn_time = static_cast<int>(config.at("PetRespawnTime"));
	double damage_multiplier = static_cast<double>(config.at("PetDamageMultiplier"));
	double pet_speed = static_cast<double>(config.at("PetSpeed"));
	int chase_distance = static_cast<int>(config.at("PetChaseDistance"));
	int guard_distance = static_cast<int>(config.at("PetGuardDistance"));

	if (this->pet && this->PetOwner)
	{
		// Check if the owner is on a different map
		if (this->PetOwner->map != this->map)
		{
			this->PetOwner->PetTransfer();
			return;
		}
		// Handle pet positioning behind the player when the player changes direction while standing still
		if (this->PetOwner->IsStandingStill() && this->PetOwner->HasChangedDirection())
		{
			// Move the pet to one tile behind the player based on the player's direction
			this->x = this->PetOwner->x - pet_dir_dx(this->PetOwner->direction);
			this->y = this->PetOwner->y - pet_dir_dy(this->PetOwner->direction);
			this->direction = this->PetOwner->direction;

			// Ensure the pet's new position is walkable
			if (!this->map->Walkable(this->x, this->y, true))
			{
				// If not walkable, keep the pet at its current position
				this->x = this->PetOwner->x;
				this->y = this->PetOwner->y;
			}

			// Notify nearby characters of the pet's movement
			PacketBuilder builder(PACKET_NPC, PACKET_PLAYER, 7);
			builder.AddChar(this->index);
			builder.AddChar(this->x);
			builder.AddChar(this->y);
			builder.AddChar(this->direction);
			builder.AddByte(255);
			builder.AddByte(255);
			builder.AddByte(255);

			UTIL_FOREACH(this->map->characters, character)
			{
				if (character->InRange(this))
				{
					character->Send(builder);
				}
			}

			// Reset the owner's direction change flag
			this->PetOwner->ResetDirectionChangeFlag();
		}
		// Handle pet following behavior
		if (this->PetFollowing)
		{
			int distance_to_owner = util::path_length(this->x, this->y, this->PetOwner->x, this->PetOwner->y);

			// Smooth movement: move step-by-step toward the owner
			if (distance_to_owner > 1)
			{
				this->PetWalkTo(this->PetOwner->x, this->PetOwner->y);
				return;
			}
			else if (distance_to_owner > (guard_distance * 2))
			{
				this->PetOwner->PetTransfer();
				return;
			}
			else
			{
				// Stand one space behind the owner and face the same direction
				this->x = this->PetOwner->x - pet_dir_dx(this->PetOwner->direction);
				this->y = this->PetOwner->y - pet_dir_dy(this->PetOwner->direction);
				this->direction = this->PetOwner->direction;
				return;
			}
		}

		// Handle pet attacking behavior
		if (this->PetAttacking)
		{
			int distance_to_owner = util::path_length(this->x, this->y, this->PetOwner->x, this->PetOwner->y);

			// If the owner has moved, prioritize returning to the owner after the current kill
			if (distance_to_owner > guard_distance)
			{
				if (this->PetTarget && this->PetTarget->alive)
				{
					// Finish attacking the current target
					int distance_to_target = util::path_length(this->x, this->y, this->PetTarget->x, this->PetTarget->y);

					if (distance_to_target > 1)
					{
						this->PetWalkTo(this->PetTarget->x, this->PetTarget->y);
					}
					else
					{
						this->PetDetermineDirection(this->PetTarget->x, this->PetTarget->y);

						// Apply damage multiplier
						int damage = util::rand(this->PetMinDamage, this->PetMaxDamage) * damage_multiplier;
						this->PetTarget->Damage(this->PetOwner, damage, -1);

						if (this->PetTarget->hp <= 0)
						{
							this->PetTarget = nullptr;
						}
					}
					return;
				}

				// Return to the owner after finishing the current target
				this->PetWalkTo(this->PetOwner->x, this->PetOwner->y);

				// Check for NPCs in range after returning to the owner
				if (distance_to_owner == 1)
				{
					this->PetTarget = nullptr; // Reset the target to find a new one
				}
				return;
			}

			// If no target or the target is dead, find a new target
			if (!this->PetTarget || !this->PetTarget->alive)
			{
				NPC *closest_npc = nullptr;
				int closest_distance = chase_distance + 1;

				UTIL_FOREACH(this->map->npcs, npc)
				{
					if (npc->alive && npc != this &&
						(npc->ENF().type == ENF::Aggressive || npc->ENF().type == ENF::Passive))
					{
						int distance = util::path_length(this->x, this->y, npc->x, npc->y);
						if (distance <= chase_distance && distance < closest_distance)
						{
							closest_npc = npc;
							closest_distance = distance;
						}
					}
				}

				this->PetTarget = closest_npc;
			}

			// Attack the target if one is found
			if (this->PetTarget)
			{
				int distance_to_target = util::path_length(this->x, this->y, this->PetTarget->x, this->PetTarget->y);

				if (distance_to_target > 1)
				{
					this->PetWalkTo(this->PetTarget->x, this->PetTarget->y);
				}
				else
				{
					this->PetDetermineDirection(this->PetTarget->x, this->PetTarget->y);

					// Apply damage multiplier
					int damage = util::rand(this->PetMinDamage, this->PetMaxDamage) * damage_multiplier;
					this->PetTarget->Damage(this->PetOwner, damage, -1);

					if (this->PetTarget->hp <= 0)
					{
						this->PetTarget = nullptr;
					}
				}
				return;
			}

			// If no valid targets are in range, return to the owner
			this->PetWalkTo(this->PetOwner->x, this->PetOwner->y);
			return;
		}

		// Handle pet guarding behavior
		if (this->PetGuarding)
		{
			int distance_to_owner = util::path_length(this->x, this->y, this->PetOwner->x, this->PetOwner->y);

			// Ensure the pet stays within PetGuardDistance of the owner
			if (distance_to_owner > guard_distance)
			{
				this->PetWalkTo(this->PetOwner->x, this->PetOwner->y);
				return;
			}

			// Check for NPCs near the owner
			NPC *nearby_npc = nullptr;

			UTIL_FOREACH(this->map->npcs, npc)
			{
				if (npc->alive && npc != this &&
					(npc->ENF().type == ENF::Aggressive || npc->ENF().type == ENF::Passive))
				{
					int distance_to_owner = util::path_length(this->PetOwner->x, this->PetOwner->y, npc->x, npc->y);

					// If an NPC is within 1 tile of the owner, target it
					if (distance_to_owner == 1)
					{
						nearby_npc = npc;
						break;
					}
				}
			}

			// Attack the nearby NPC if found
			if (nearby_npc)
			{
				this->PetTarget = nearby_npc;

				int distance_to_target = util::path_length(this->x, this->y, this->PetTarget->x, this->PetTarget->y);

				if (distance_to_target > 1)
				{
					this->PetWalkTo(this->PetTarget->x, this->PetTarget->y);
				}
				else
				{
					this->PetDetermineDirection(this->PetTarget->x, this->PetTarget->y);

					// Apply damage multiplier
					int damage = util::rand(this->PetMinDamage, this->PetMaxDamage) * damage_multiplier;
					this->PetTarget->Damage(this->PetOwner, damage, -1);

					if (this->PetTarget->hp <= 0)
					{
						this->PetTarget = nullptr;
					}
				}
				return;
			}

			// If no NPC is nearby, stay close to the owner
			this->PetWalkTo(this->PetOwner->x, this->PetOwner->y);
			return;
		}
	}

	Character *attacker = 0;
	unsigned char attacker_distance = static_cast<int>(this->map->world->config["NPCChaseDistance"]);
	unsigned short attacker_damage = 0;

	if (this->ENF().type == ENF::Passive || this->ENF().type == ENF::Aggressive)
	{
		UTIL_FOREACH_CREF(this->damagelist, opponent)
		{
			if (opponent->attacker->map != this->map || opponent->attacker->nowhere || opponent->last_hit < Timer::GetTime() - static_cast<double>(this->map->world->config["NPCBoredTimer"]))
			{
				continue;
			}

			int distance = util::path_length(opponent->attacker->x, opponent->attacker->y, this->x, this->y);

			if (distance == 0)
				distance = 1;

			if ((distance < attacker_distance) || (distance == attacker_distance && opponent->damage > attacker_damage))
			{
				attacker = opponent->attacker;
				attacker_damage = opponent->damage;
				attacker_distance = distance;
			}
		}

		if (this->parent)
		{
			UTIL_FOREACH_CREF(this->parent->damagelist, opponent)
			{
				if (opponent->attacker->map != this->map || opponent->attacker->nowhere || opponent->last_hit < Timer::GetTime() - static_cast<double>(this->map->world->config["NPCBoredTimer"]))
				{
					continue;
				}

				int distance = util::path_length(opponent->attacker->x, opponent->attacker->y, this->x, this->y);

				if (distance == 0)
					distance = 1;

				if ((distance < attacker_distance) || (distance == attacker_distance && opponent->damage > attacker_damage))
				{
					attacker = opponent->attacker;
					attacker_damage = opponent->damage;
					attacker_distance = distance;
				}
			}
		}
	}

	if (this->ENF().type == ENF::Aggressive || (this->parent && attacker))
	{
		Character *closest = 0;
		unsigned char closest_distance = static_cast<int>(this->map->world->config["NPCChaseDistance"]);

		if (attacker)
		{
			closest = attacker;
			closest_distance = std::min(closest_distance, attacker_distance);
		}

		UTIL_FOREACH(this->map->characters, character)
		{
			if (character->IsHideNpc() || !character->CanInteractCombat())
				continue;

			int distance = util::path_length(character->x, character->y, this->x, this->y);

			if (distance == 0)
				distance = 1;

			if (distance < closest_distance)
			{
				closest = character;
				closest_distance = distance;
			}
		}

		if (closest)
		{
			attacker = closest;
		}
	}

	if (attacker)
	{
		int xdiff = this->x - attacker->x;
		int ydiff = this->y - attacker->y;
		int absxdiff = std::abs(xdiff);
		int absydiff = std::abs(ydiff);

		if ((absxdiff == 1 && absydiff == 0) || (absxdiff == 0 && absydiff == 1) || (absxdiff == 0 && absydiff == 0))
		{
			this->Attack(attacker);
			return;
		}
		else if (absxdiff > absydiff)
		{
			if (xdiff < 0)
			{
				this->direction = DIRECTION_RIGHT;
			}
			else
			{
				this->direction = DIRECTION_LEFT;
			}
		}
		else
		{
			if (ydiff < 0)
			{
				this->direction = DIRECTION_DOWN;
			}
			else
			{
				this->direction = DIRECTION_UP;
			}
		}

		if (this->Walk(this->direction) == Map::WalkFail)
		{
			if (this->direction == DIRECTION_UP || this->direction == DIRECTION_DOWN)
			{
				if (xdiff < 0)
				{
					this->direction = DIRECTION_RIGHT;
				}
				else
				{
					this->direction = DIRECTION_LEFT;
				}
			}

			if (this->Walk(static_cast<Direction>(this->direction)) == Map::WalkFail)
			{
				this->Walk(static_cast<Direction>(util::rand(0, 3)));
			}
		}
	}
	else
	{
		// Random walking

		int act;
		if (this->walk_idle_for == 0)
		{
			act = util::rand(1, 10);
		}
		else
		{
			--this->walk_idle_for;
			act = 11;
		}

		if (act >= 1 && act <= 6) // 60% chance walk foward
		{
			this->Walk(this->direction);
		}

		if (act >= 7 && act <= 9) // 30% change direction
		{
			this->Walk(static_cast<Direction>(util::rand(0, 3)));
		}

		if (act == 10) // 10% take a break
		{
			this->walk_idle_for = util::rand(1, 4);
		}
	}
}

bool NPC::Walk(Direction direction)
{
	if (this->ENF().type == ENF::Pet)
	{
		// Treat pet movement as player movement
		return this->map->Walkable(this->x, this->y, false) && this->map->Walk(this, direction);
	}

	return this->map->Walk(this, direction);
}

void NPC::Damage(Character *from, int amount, int spell_id)
{
	int limitamount = std::min(this->hp, amount);

	if (this->map->world->config["LimitDamage"])
	{
		amount = limitamount;
	}

	if (this->ENF().type == ENF::Passive || this->ENF().type == ENF::Aggressive)
	{
		this->hp -= limitamount;
	}
	else
	{
		this->hp = 0;
		amount = 0;
	}

	if (this->totaldamage + limitamount > this->totaldamage)
		this->totaldamage += limitamount;

	// Check if the attacker already exists in the damagelist
	bool found = false;
	UTIL_FOREACH_CREF(this->damagelist, checkopp)
	{
		if (checkopp->attacker == from) // Match the attacker
		{
			found = true;

			// Increment the damage dealt by the attacker
			checkopp->damage += amount;

			// Update the last hit timestamp
			checkopp->last_hit = Timer::GetTime();
			break;
		}
	}

	// If the attacker is not found, add them to the damagelist
	if (!found)
	{
		auto opponent = std::make_unique<NPC_Opponent>();
		opponent->attacker = from;			   // Set the attacker
		opponent->damage = amount;			   // Set the damage dealt
		opponent->last_hit = Timer::GetTime(); // Set the timestamp

		// Add the new attacker to the damagelist
		this->damagelist.emplace_back(std::move(opponent));

		// Register the NPC in the attacker's unregister list
		from->unregister_npc.push_back(this);
	}

	if (this->hp > 0)
	{
		PacketBuilder builder(spell_id == -1 ? PACKET_NPC : PACKET_CAST, PACKET_REPLY, 14);

		if (spell_id != -1)
			builder.AddShort(spell_id);

		builder.AddShort(from->PlayerID());
		builder.AddChar(from->direction);
		builder.AddShort(this->index);
		builder.AddThree(amount);
		builder.AddShort(util::clamp<int>(double(this->hp) / double(this->ENF().hp) * 100.0, 0, 100));

		if (spell_id != -1)
			builder.AddShort(from->tp);
		else
			builder.AddChar(1); // ?

		UTIL_FOREACH(this->map->characters, character)
		{
			if (character->InRange(this))
			{
				character->Send(builder);
			}
		}
	}
	else
	{
		this->Killed(from, amount, spell_id);
		// *this may not be valid here
		return;
	}
}

void NPC::RemoveFromView(Character *target)
{
	PacketBuilder builder(PACKET_NPC, PACKET_PLAYER, 7);
	builder.AddChar(this->index);
	if (target->x > 200 && target->y > 200)
	{
		builder.AddChar(0); // x
		builder.AddChar(0); // y
	}
	else
	{
		builder.AddChar(252); // x
		builder.AddChar(252); // y
	}
	builder.AddChar(0); // direction
	builder.AddByte(255);
	builder.AddByte(255);
	builder.AddByte(255);

	PacketBuilder builder2(PACKET_NPC, PACKET_SPEC, 5);
	builder2.AddShort(0); // killer pid
	builder2.AddChar(0);  // killer direction
	builder2.AddShort(this->index);
	/*
		builder2.AddShort(0); // dropped item uid
		builder2.AddShort(0); // dropped item id
		builder2.AddChar(this->x);
		builder2.AddChar(this->y);
		builder2.AddInt(0); // dropped item amount
		builder2.AddThree(0); // damage
	*/

	target->Send(builder);
	target->Send(builder2);
}

void NPC::Killed(Character *from, int amount, int spell_id)
{
	double droprate = this->map->world->config["DropRate"];
	double exprate = this->map->world->config["ExpRate"];
	int sharemode = this->map->world->config["ShareMode"];
	int partysharemode = this->map->world->config["PartyShareMode"];
	int dropratemode = this->map->world->config["DropRateMode"];
	std::set<Party *> parties;

	int most_damage_counter = 0;
	Character *most_damage = nullptr;
	NPC_Drop *drop = nullptr;

	this->alive = false;

	this->dead_since = int(Timer::GetTime());

	if (dropratemode == 1)
	{
		std::vector<NPC_Drop *> drops;

		UTIL_FOREACH_CREF(this->Data().drops, checkdrop)
		{
			if (util::rand(0.0, 100.0) <= checkdrop->chance * droprate)
			{
				drops.push_back(checkdrop.get());
			}
		}

		if (drops.size() > 0)
		{
			drop = drops[util::rand(0, drops.size() - 1)];
		}
	}
	else if (dropratemode == 2)
	{
		UTIL_FOREACH_CREF(this->Data().drops, checkdrop)
		{
			if (util::rand(0.0, 100.0) <= checkdrop->chance * droprate)
			{
				drop = checkdrop.get();
				break;
			}
		}
	}
	else if (dropratemode == 3)
	{
		double roll = util::rand(0.0, this->Data().drops_chance_total);

		UTIL_FOREACH_CREF(this->Data().drops, checkdrop)
		{
			if (roll >= checkdrop->chance_offset && roll < checkdrop->chance_offset + checkdrop->chance)
			{
				drop = checkdrop.get();
				break;
			}
		}
	}

	if (sharemode == 1)
	{
		UTIL_FOREACH_CREF(this->damagelist, opponent)
		{
			if (opponent->damage > most_damage_counter)
			{
				most_damage_counter = opponent->damage;
				most_damage = opponent->attacker;
			}
		}
	}

	int dropuid = 0;
	int dropid = 0;
	int dropamount = 0;
	Character *drop_winner = nullptr;

	if (drop)
	{
		dropid = drop->id;
		dropamount = std::min<int>(util::rand(drop->min, drop->max), this->map->world->config["MaxItem"]);

		if (dropid <= 0 || static_cast<std::size_t>(dropid) >= this->map->world->eif->data.size() || dropamount <= 0)
			goto abort_drop;

		dropuid = this->map->GenerateItemID();

		std::shared_ptr<Map_Item> newitem(std::make_shared<Map_Item>(dropuid, dropid, dropamount, this->x, this->y, from->PlayerID(), Timer::GetTime() + static_cast<int>(this->map->world->config["ProtectNPCDrop"])));
		this->map->items.push_back(newitem);

		// Selects a random number between 0 and maxhp, and decides the winner based on that
		switch (sharemode)
		{
		case 0:
			drop_winner = from;
			break;

		case 1:
			drop_winner = most_damage;
			break;

		case 2:
		{
			int rewarded_hp = util::rand(0, this->totaldamage - 1);
			int count_hp = 0;
			UTIL_FOREACH_CREF(this->damagelist, opponent)
			{
				if (opponent->attacker->InRange(this))
				{
					if (rewarded_hp >= count_hp && rewarded_hp < opponent->damage)
					{
						drop_winner = opponent->attacker;
						break;
					}

					count_hp += opponent->damage;
				}
			}
		}
		break;

		case 3:
		{
			int rand = util::rand(0, this->damagelist.size() - 1);
			int i = 0;
			UTIL_FOREACH_CREF(this->damagelist, opponent)
			{
				if (opponent->attacker->InRange(this))
				{
					if (rand == i++)
					{
						drop_winner = opponent->attacker;
						break;
					}
				}
			}
		}
		break;
		}
	}
abort_drop:

	if (drop_winner)
	{
		if (drop_winner->autoloot_enabled)
		{
			// Autoloot is enabled; attempt to pick up the item
			int taken = drop_winner->CanHoldItem(dropid, dropamount);

			if (taken > 0)
			{
				drop_winner->AddItem(dropid, taken);
				PacketBuilder reply(PACKET_ITEM, PACKET_GET, 9);
				reply.AddShort(0); // UID
				reply.AddShort(dropid);
				reply.AddThree(taken);
				reply.AddChar(drop_winner->weight);
				reply.AddChar(drop_winner->maxweight);
				drop_winner->Send(reply);

				EIF_Data &data = drop_winner->world->eif->Get(dropid);
				for (Character *other : drop_winner->map->characters)
				{
					if (other != drop_winner && drop_winner->InRange(other))
					{
						other->StatusMsg(drop_winner->real_name + " picked up x" + util::to_string(taken) + " " + data.name);
					}
				}

				dropuid = 0;
				dropid = 0;
				dropamount = 0;
				this->map->items.pop_back(); // Remove the item from the map
			}
		}
		else
		{
			// Autoloot is disabled; set the item's owner to the drop winner
			this->map->items.back()->owner = drop_winner->PlayerID();
		}
	}

	UTIL_FOREACH(this->map->characters, character)
	{
		std::list<std::unique_ptr<NPC_Opponent>>::iterator findopp = this->damagelist.begin();
		for (; findopp != this->damagelist.end() && (*findopp)->attacker != character; ++findopp)
			; // no loop body

		if (findopp != this->damagelist.end() || character->InRange(this))
		{
			bool level_up = false;

			PacketBuilder builder(spell_id == -1 ? PACKET_NPC : PACKET_CAST, PACKET_SPEC, 26);

			if (this->ENF().exp != 0)
			{
				if (findopp != this->damagelist.end())
				{
					int reward;
					switch (sharemode)
					{
					case 0:
						if (character == from)
						{
							reward = int(std::ceil(double(this->ENF().exp) * exprate));

							if (reward > 0)
							{
								if (partysharemode)
								{
									if (character->party)
									{
										character->party->ShareEXP(reward, partysharemode, this->map);
									}
									else
									{
										character->exp += reward;
									}
								}
								else
								{
									character->exp += reward;
								}
							}
						}
						break;

					case 1:
						if (character == most_damage)
						{
							reward = int(std::ceil(double(this->ENF().exp) * exprate));

							if (reward > 0)
							{
								if (partysharemode)
								{
									if (character->party)
									{
										character->party->ShareEXP(reward, partysharemode, this->map);
									}
									else
									{
										character->exp += reward;
									}
								}
								else
								{
									character->exp += reward;
								}
							}
						}
						break;

					case 2:
						reward = int(std::ceil(double(this->ENF().exp) * exprate * (double((*findopp)->damage) / double(this->totaldamage))));

						if (reward > 0)
						{
							if (partysharemode)
							{
								if (character->party)
								{
									character->party->temp_expsum += reward;
									parties.insert(character->party);
								}
								else
								{
									character->exp += reward;
								}
							}
							else
							{
								character->exp += reward;
							}
						}
						break;

					case 3:
						reward = int(std::ceil(double(this->ENF().exp) * exprate * (double(this->damagelist.size()) / 1.0)));

						if (reward > 0)
						{
							if (partysharemode)
							{
								if (character->party)
								{
									character->party->temp_expsum += reward;
								}
								else
								{
									character->exp += reward;
								}
							}
							else
							{
								character->exp += reward;
							}
						}
						break;
					}

					character->exp = std::min(character->exp, static_cast<int>(this->map->world->config["MaxExp"]));

					while (character->level < static_cast<int>(this->map->world->config["MaxLevel"]) && character->exp >= this->map->world->exp_table[character->level + 1])
					{
						level_up = true;
						++character->level;
						character->statpoints += static_cast<int>(this->map->world->config["StatPerLevel"]);
						character->skillpoints += static_cast<int>(this->map->world->config["SkillPerLevel"]);
						character->CalculateStats();
					}

					if (level_up)
					{
						builder.SetID(spell_id == -1 ? PACKET_NPC : PACKET_CAST, PACKET_ACCEPT);
						builder.ReserveMore(33);
					}
				}
			}

			if (spell_id != -1)
				builder.AddShort(spell_id);

			builder.AddShort(drop_winner ? drop_winner->PlayerID() : from->PlayerID());
			builder.AddChar(drop_winner ? drop_winner->direction : from->direction);
			builder.AddShort(this->index);
			builder.AddShort(dropuid);
			builder.AddShort(dropid);
			builder.AddChar(this->x);
			builder.AddChar(this->y);
			builder.AddInt(dropamount);
			builder.AddThree(amount);

			if (spell_id != -1)
				builder.AddShort(from->tp);

			if ((sharemode == 0 && character == from) || (sharemode != 0 && findopp != this->damagelist.end()))
			{
				builder.AddInt(character->exp);
			}

			if (level_up)
			{
				builder.AddChar(character->level);
				builder.AddShort(character->statpoints);
				builder.AddShort(character->skillpoints);
				builder.AddShort(character->maxhp);
				builder.AddShort(character->maxtp);
				builder.AddShort(character->maxsp);
			}

			character->Send(builder);
		}
	}

	UTIL_FOREACH(parties, party)
	{
		party->ShareEXP(party->temp_expsum, partysharemode, this->map);
		party->temp_expsum = 0;
	}

	UTIL_FOREACH_CREF(this->damagelist, opponent)
	{
		opponent->attacker->unregister_npc.erase(
			std::remove(UTIL_RANGE(opponent->attacker->unregister_npc), this),
			opponent->attacker->unregister_npc.end());
	}

	this->damagelist.clear();
	this->totaldamage = 0;

	short childid = -1;

	if (this->ENF().boss)
	{
		std::vector<NPC *> child_npcs;

		UTIL_FOREACH(this->map->npcs, npc)
		{
			if (npc->ENF().child && !npc->ENF().boss && npc->alive)
			{
				child_npcs.push_back(npc);
			}
		}

		UTIL_FOREACH(child_npcs, npc)
		{
			if (!npc->temporary && (childid == -1 || childid == npc->id))
			{
				npc->Die(false);
				childid = npc->id;
			}
			else
			{
				npc->Die(true);
			}
		}
	}

	if (childid != -1)
	{
		PacketBuilder builder(PACKET_NPC, PACKET_JUNK, 2);
		builder.AddShort(childid);

		UTIL_FOREACH(this->map->characters, character)
		{
			character->Send(builder);
		}
	}

	if (this->temporary)
	{
		this->map->npcs.erase(
			std::remove(this->map->npcs.begin(), this->map->npcs.end(), this),
			this->map->npcs.end());
	}

	UTIL_FOREACH(from->quests, q)
	{
		if (!q.second || q.second->GetQuest()->Disabled())
			continue;

		q.second->KilledNPC(this->ENF().id);
	}

	if (from->party) // Party kills
	{
		UTIL_FOREACH(from->party->members, member)
		{
			if (from != member)
			{
				UTIL_FOREACH(member->quests, q)
				{
					if (!q.second || q.second->GetQuest()->Disabled())
						continue;

					if (member->map == this->map)
					{
						q.second->KilledNPC(this->ENF().id);
					}
				}
			}
		}
	}

	if (this->temporary)
	{
		delete this;
		return;
	}
}

void NPC::Die(bool show)
{
	if (!this->alive)
		return;

	this->alive = false;
	this->parent = 0;
	this->dead_since = int(Timer::GetTime());

	UTIL_FOREACH_CREF(this->damagelist, opponent)
	{
		opponent->attacker->unregister_npc.erase(
			std::remove(UTIL_RANGE(opponent->attacker->unregister_npc), this),
			opponent->attacker->unregister_npc.end());
	}

	this->damagelist.clear();
	this->totaldamage = 0;

	if (show)
	{
		PacketBuilder builder(PACKET_NPC, PACKET_SPEC, 18);
		builder.AddShort(0); // killer pid
		builder.AddChar(0);	 // killer direction
		builder.AddShort(this->index);
		builder.AddShort(0); // dropped item uid
		builder.AddShort(0); // dropped item id
		builder.AddChar(this->x);
		builder.AddChar(this->y);
		builder.AddInt(0);			// dropped item amount
		builder.AddThree(this->hp); // damage

		UTIL_FOREACH(this->map->characters, character)
		{
			if (character->InRange(this))
			{
				character->Send(builder);
			}
		}
	}

	if (this->temporary)
	{
		this->map->npcs.erase(
			std::remove(this->map->npcs.begin(), this->map->npcs.end(), this),
			this->map->npcs.end());

		delete this;
	}
}

void NPC::Attack(Character *target)
{
	int amount = util::rand(this->ENF().mindam, this->ENF().maxdam + static_cast<int>(this->map->world->config["NPCAdjustMaxDam"]));
	double rand = util::rand(0.0, 1.0);
	// Checks if target is facing you
	bool critical = std::abs(int(target->direction) - this->direction) != 2 || rand < static_cast<double>(this->map->world->config["CriticalRate"]);

	std::unordered_map<std::string, double> formula_vars;

	this->FormulaVars(formula_vars);
	target->FormulaVars(formula_vars, "target_");
	formula_vars["modifier"] = 1.0 / static_cast<double>(this->map->world->config["MobRate"]);
	formula_vars["damage"] = amount;
	formula_vars["critical"] = critical;

	amount = this->map->world->EvalFormula("damage", formula_vars);
	double hit_rate = this->map->world->EvalFormula("hit_rate", formula_vars);

	if (rand > hit_rate)
	{
		amount = 0;
	}

	amount = std::max(amount, 0);

	int limitamount = std::min(amount, int(target->hp));

	if (this->map->world->config["LimitDamage"])
	{
		amount = limitamount;
	}

	target->hp -= limitamount;
	if (target->party)
	{
		target->party->UpdateHP(target);
	}

	int xdiff = this->x - target->x;
	int ydiff = this->y - target->y;

	if (std::abs(xdiff) > std::abs(ydiff))
	{
		if (xdiff < 0)
		{
			this->direction = DIRECTION_RIGHT;
		}
		else
		{
			this->direction = DIRECTION_LEFT;
		}
	}
	else
	{
		if (ydiff < 0)
		{
			this->direction = DIRECTION_DOWN;
		}
		else
		{
			this->direction = DIRECTION_UP;
		}
	}

	PacketBuilder builder(PACKET_NPC, PACKET_PLAYER, 18);
	builder.AddByte(255);
	builder.AddChar(this->index);
	builder.AddChar(1 + (target->hp == 0));
	builder.AddChar(this->direction);
	builder.AddShort(target->PlayerID());
	builder.AddThree(amount);
	builder.AddThree(util::clamp<int>(double(target->hp) / double(target->maxhp) * 100.0, 0, 100));
	builder.AddByte(255);
	builder.AddByte(255);

	UTIL_FOREACH(this->map->characters, character)
	{
		if (character == target || !character->InRange(target))
		{
			continue;
		}

		character->Send(builder);
	}

	if (target->hp == 0)
	{
		target->DeathRespawn();
	}

	builder.AddShort(target->hp);
	builder.AddShort(target->tp);

	target->Send(builder);
}

void NPC::Say(const std::string &message)
{
	PacketBuilder builder(PACKET_NPC, PACKET_PLAYER, 5 + message.length());
	builder.AddByte(255);
	builder.AddByte(255);
	builder.AddChar(this->index);
	builder.AddChar(message.length());
	builder.AddString(message.c_str());
	builder.AddByte(255);

	UTIL_FOREACH(this->map->characters, character)
	{
		if (!character->InRange(this))
		{
			continue;
		}

		character->Send(builder);
	}
}

#define v(x) vars[prefix + #x] = x;
#define vv(x, n) vars[prefix + n] = x;
#define vd(x) vars[prefix + #x] = data.x;

void NPC::FormulaVars(std::unordered_map<std::string, double> &vars, std::string prefix)
{
	const ENF_Data &data = this->ENF();
	vv(1, "npc") v(hp) vv(data.hp, "maxhp")
		vd(mindam) vd(maxdam)
			vd(accuracy) vd(evade) vd(armor)
				v(x) v(y) v(direction) vv(map->id, "mapid")
}

#undef vd
#undef vv
#undef v

void NPC::PetSetOwner(Character *character)
{
	this->PetOwner = character;
	this->pet = true;
	this->PetFollowing = true;
	this->PetGuarding = false;
	this->PetAttacking = false;
}

void NPC::PetDamage(NPC *from, int amount, int spell_id)
{
	int limitamount = std::min(this->hp, amount);

	if (this->map->world->config["LimitDamage"])
	{
		amount = limitamount;
	}

	if (this->ENF().type == ENF::Passive || this->ENF().type == ENF::Aggressive)
	{
		this->hp -= amount;
	}
	else
	{
		this->hp = 0;
		amount = 0;
	}

	if (this->totaldamage + limitamount > this->totaldamage)
		this->totaldamage += limitamount;

	auto opponent = std::make_unique<NPC_Opponent>();
	bool found = false;

	// Use references to avoid copying unique_ptr
	UTIL_FOREACH(this->damagelist, &checkopp)
	{
		if (checkopp->attacker == from->PetOwner)
		{
			found = true;

			if (checkopp->damage + limitamount > checkopp->damage)
				checkopp->damage += limitamount;

			checkopp->last_hit = Timer::GetTime();
		}
	}

	if (!found)
	{
		opponent->attacker = from->PetOwner;
		opponent->damage = limitamount;
		opponent->last_hit = Timer::GetTime();
		this->damagelist.push_back(std::move(opponent));
		from->PetOwner->unregister_npc.push_back(this);
	}

	if (this->hp > 0)
	{
		PacketBuilder builder(spell_id == -1 ? PACKET_NPC : PACKET_CAST, PACKET_REPLY, 14);

		if (spell_id != -1)
			builder.AddShort(spell_id);

		builder.AddShort(from->index);
		builder.AddChar(from->direction);
		builder.AddShort(this->index);
		builder.AddThree(amount);
		builder.AddShort(util::clamp<int>(double(this->hp) / double(this->ENF().hp) * 100.0, 0, 100));

		if (spell_id != -1)
			builder.AddShort(from->PetOwner->tp);
		else
			builder.AddChar(1);

		UTIL_FOREACH(this->map->characters, character)
		{
			if (character->InRange(this))
			{
				character->Send(builder);
			}
		}
	}
	else
	{
		if (this == from->PetTarget)
		{
			from->PetTarget = nullptr;
		}
		this->Killed(from->PetOwner, amount, spell_id);
	}
}

void NPC::PetWalkTo(int x, int y)
{
	int xdiff = this->x - x;
	int ydiff = this->y - y;
	int absxdiff = std::abs(xdiff);
	int absydiff = std::abs(ydiff);

	if ((absxdiff == 1 && absydiff == 0) || (absxdiff == 0 && absydiff == 1) || (absxdiff == 0 && absydiff == 0))
	{
		return;
	}
	else if (absxdiff > absydiff)
	{
		this->direction = (xdiff < 0) ? DIRECTION_RIGHT : DIRECTION_LEFT;
	}
	else
	{
		this->direction = (ydiff < 0) ? DIRECTION_DOWN : DIRECTION_UP;
	}

	if (!this->Walk(this->direction))
	{
		this->Walk(static_cast<Direction>(util::rand(0, 3)));
	}
}

void NPC::PetDetermineDirection(int x, int y)
{
	int xdiff = x - this->x;
	int ydiff = y - this->y;

	if (std::abs(xdiff) > std::abs(ydiff))
	{
		this->direction = (xdiff > 0) ? DIRECTION_RIGHT : DIRECTION_LEFT;
	}
	else
	{
		this->direction = (ydiff > 0) ? DIRECTION_DOWN : DIRECTION_UP;
	}
}

void NPC::PetFindAltRoute(int target_x, int target_y)
{
	std::vector<Direction> path;
	if (this->PetFindPath(target_x, target_y, path) && !path.empty())
	{
		this->Walk(path.front());
	}
	else
	{
		// Fall back to random movement if no path is found
		this->Walk(static_cast<Direction>(util::rand(0, 3)));
	}
}

NPC *NPC::PetFindNearbyEnemy()
{
	NPC *closest_enemy = nullptr;
	unsigned char closest_distance = static_cast<unsigned char>(static_cast<int>(this->map->world->config["PetChaseDistance"]));

	UTIL_FOREACH(this->map->npcs, npc)
	{
		// Skip dead NPCs or NPCs of the same owner
		if (!npc->alive || npc == this || npc->PetOwner == this->PetOwner)
			continue;

		// Calculate distance to the NPC
		unsigned char distance = util::path_length(this->x, this->y, npc->x, npc->y);

		// Update the closest enemy if within range
		if (distance < closest_distance)
		{
			closest_enemy = npc;
			closest_distance = distance;
		}
	}

	return closest_enemy;
}

bool NPC::PetFindPath(int target_x, int target_y, std::vector<Direction> &path)
{
	// Define the PetNode struct for A* pathfinding
	struct PetNode
	{
		int x, y;
		int g, h;
		PetNode *parent;

		PetNode(int x, int y, int g, int h, PetNode *parent = nullptr)
			: x(x), y(y), g(g), h(h), parent(parent) {}

		int f() const { return g + h; }

		bool operator<(const PetNode &other) const { return f() > other.f(); }
	};

	std::priority_queue<PetNode> open_list;
	std::unordered_set<int> closed_set;

	auto hash = [this](int x, int y)
	{
		return y * this->map->width + x;
	};

	open_list.emplace(this->x, this->y, 0, util::path_length(this->x, this->y, target_x, target_y));

	while (!open_list.empty())
	{
		PetNode current = open_list.top();
		open_list.pop();

		if (current.x == target_x && current.y == target_y)
		{
			// Reconstruct path
			while (current.parent)
			{
				int dx = current.x - current.parent->x;
				int dy = current.y - current.parent->y;

				if (dx == 1)
					path.push_back(DIRECTION_RIGHT);
				else if (dx == -1)
					path.push_back(DIRECTION_LEFT);
				else if (dy == 1)
					path.push_back(DIRECTION_DOWN);
				else if (dy == -1)
					path.push_back(DIRECTION_UP);

				current = *current.parent;
			}

			std::reverse(path.begin(), path.end());
			return true;
		}

		int current_hash = hash(current.x, current.y);
		if (closed_set.count(current_hash))
			continue;

		closed_set.insert(current_hash);

		Direction directions[] = {DIRECTION_UP, DIRECTION_DOWN, DIRECTION_LEFT, DIRECTION_RIGHT};
		for (Direction dir : directions)
		{
			int nx = current.x, ny = current.y;
			switch (dir)
			{
			case DIRECTION_UP:
				--ny;
				break;
			case DIRECTION_DOWN:
				++ny;
				break;
			case DIRECTION_LEFT:
				--nx;
				break;
			case DIRECTION_RIGHT:
				++nx;
				break;
			}

			if (!this->map->Walkable(nx, ny, true) || closed_set.count(hash(nx, ny)))
				continue;

			int g = current.g + 1;
			int h = util::path_length(nx, ny, target_x, target_y);
			open_list.emplace(nx, ny, g, h, new PetNode(current));
		}
	}

	// If no path is found, fallback to random movement
	path.clear();
	path.push_back(static_cast<Direction>(util::rand(0, 3)));
	return false;
}

void NPC::ResetDirectionChangeFlag()
{
	this->direction_changed = false;
}

bool NPC::HasChangedDirection() const
{
	return this->direction_changed;
}

NPC::~NPC()
{
	UTIL_FOREACH(this->map->characters, character)
	{
		if (character->npc == this)
		{
			character->npc = 0;
			character->npc_type = ENF::NPC;
		}
	}

	UTIL_FOREACH_CREF(this->damagelist, opponent)
	{
		opponent->attacker->unregister_npc.erase(
			std::remove(UTIL_RANGE(opponent->attacker->unregister_npc), this),
			opponent->attacker->unregister_npc.end());
	}
}
