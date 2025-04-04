/* party.cpp
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "party.hpp"

#include "character.hpp"
#include "config.hpp"
#include "packet.hpp"
#include "world.hpp"

#include "util.hpp"

#include <algorithm>
#include <cmath>
#include <string>

Party::Party(World *world, Character *leader, Character *other)
{
	this->world = world;
	this->world->parties.push_back(this);

	this->members.push_back(leader);
	this->members.push_back(other);

	this->leader = leader;

	leader->party = this;
	other->party = this;

	this->temp_expsum = 0;

	this->RefreshMembers(leader, true);
	this->RefreshMembers(other, true);
}

void Party::Msg(Character *from, std::string message, bool echo)
{
	message = util::text_cap(message, static_cast<int>(this->world->config["ChatMaxWidth"]) - util::text_width(util::ucfirst(from->SourceName()) + "  "));

	PacketBuilder builder(PACKET_TALK, PACKET_OPEN, 2 + message.length());

	builder.AddShort(from->PlayerID());
	builder.AddString(message);

	UTIL_FOREACH(this->members, member)
	{
		member->AddChatLog("'", from->SourceName(), message);

		if (!echo && member == from)
			continue;

		member->Send(builder);
	}
}

void Party::Join(Character *character)
{
	character->party = this;

	this->members.push_back(character);

	PacketBuilder builder(PACKET_PARTY, PACKET_ADD, 5 + character->SourceName().length());
	builder.AddShort(character->PlayerID());
	builder.AddChar(character == this->leader);
	builder.AddChar(character->level);
	builder.AddChar(util::clamp<int>(double(character->hp) / double(character->maxhp) * 100.0, 0, 100));
	builder.AddString(character->SourceName());

	UTIL_FOREACH(this->members, checkcharacter)
	{
		if (checkcharacter != character)
		{
			checkcharacter->Send(builder);
		}
	}

	this->RefreshMembers(character, true);
}

void Party::Leave(Character *character)
{
	if (this->members.size() > 2 && character != this->leader)
	{
		UTIL_IFOREACH(this->members, checkcharacter)
		{
			if (*checkcharacter == character)
			{
				this->members.erase(checkcharacter);
				break;
			}
		}

		character->party = 0;

		PacketBuilder builder(PACKET_PARTY, PACKET_REMOVE, 2);
		builder.AddShort(character->PlayerID());
		UTIL_FOREACH(this->members, checkcharacter)
		{
			if (character != checkcharacter)
			{
				checkcharacter->Send(builder);
			}
		}

		builder.Reset(2);
		builder.SetID(PACKET_PARTY, PACKET_CLOSE);

		builder.AddShort(255);

		character->Send(builder);
	}
	else
	{
		delete this;
	}
}

void Party::RefreshMembers(Character *character, bool create)
{
	PacketBuilder builder(PACKET_PARTY, create ? PACKET_CREATE : PACKET_LIST, this->members.size() * 18);

	UTIL_FOREACH(this->members, member)
	{
		builder.AddShort(member->PlayerID());
		builder.AddChar(member == this->leader);
		builder.AddChar(member->level);
		builder.AddChar(util::clamp<int>(double(member->hp) / double(member->maxhp) * 100.0, 0, 100));
		builder.AddBreakString(member->SourceName());
	}

	character->Send(builder);
}

void Party::UpdateHP(Character *character)
{
	PacketBuilder builder(PACKET_PARTY, PACKET_AGREE, 3);
	builder.AddShort(character->PlayerID());
	builder.AddChar(util::clamp<int>(double(character->hp) / double(character->maxhp) * 100.0, 0, 100));

	UTIL_FOREACH(this->members, member)
	{
		member->Send(builder);
	}
}

void Party::ShareEXP(int exp, int sharemode, Map *map)
{
	int reward = 0;
	double sumlevel = 0;
	double members = 0;

	UTIL_FOREACH(this->members, member)
	{
		if (!member->CanInteractCombat())
			continue;

		if (member->map == map && !member->nowhere)
		{
			if (member->level == 0)
			{
				++sumlevel;
			}
			else
			{
				sumlevel += member->level;
			}
			++members;
		}
	}

	if (members == 0 || sumlevel == 0)
		return;

	UTIL_FOREACH(this->members, member)
	{
		if (!member->CanInteractCombat())
			continue;

		if (member->map != map || member->nowhere)
		{
			continue;
		}
		switch (sharemode)
		{
		case 1:
			reward = int(std::ceil(double(exp) / members));
			break;

		case 2:
			reward = int(std::ceil(double(exp) * double((member->level == 0) ? 1 : member->level) / sumlevel));
			break;
		}

		member->exp += reward;

		while (member->level < static_cast<int>(this->world->config["MaxLevel"]) && member->exp >= this->world->exp_table[member->level + 1])
		{
			member->exp -= this->world->exp_table[member->level + 1];
			++member->level;
			member->statpoints += static_cast<int>(this->world->config["StatPerLevel"]);
			member->skillpoints += static_cast<int>(this->world->config["SkillPerLevel"]);
			member->CalculateStats();
		}

		bool level_up = (member->level < static_cast<int>(this->world->config["MaxLevel"]) && member->exp >= this->world->exp_table[member->level + 1]);

		PacketBuilder builder(PACKET_PARTY, PACKET_TARGET_GROUP, 7);
		builder.AddShort(member->PlayerID());
		builder.AddInt(reward);
		builder.AddChar(level_up);

		member->Send(builder);
	}
}

Party::~Party()
{
	if (this->world)
	{
		this->world->parties.erase(
			std::remove(this->world->parties.begin(), this->world->parties.end(), this),
			this->world->parties.end());
	}

	PacketBuilder builder(PACKET_PARTY, PACKET_CLOSE, 1);
	builder.AddByte(255); // ?

	UTIL_FOREACH(this->members, member)
	{
		member->party = 0;
		member->Send(builder);
	}
}
