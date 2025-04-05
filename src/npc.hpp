/* npc.hpp
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef NPC_HPP_INCLUDED
#define NPC_HPP_INCLUDED

#include "fwd/npc.hpp"

#include "fwd/character.hpp"
#include "fwd/eodata.hpp"
#include "fwd/map.hpp"
#include "fwd/npc_data.hpp"

#include <array>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * Used by the NPC class to store information about an attacker
 */
struct NPC_Opponent
{
	Character *attacker;
	int damage;
	double last_hit;
};

/**
 * Enum representing different pet modes
 */
enum class PetMode
{
	Attacking,
	Guarding,
	Following
};

/**
 * An instance of an NPC created and managed by a Map
 */
class NPC
{
public:
	bool temporary;
	Direction direction;
	unsigned char x, y;
	NPC *parent;
	bool alive;
	double dead_since;
	double last_act;
	double act_speed;
	int walk_idle_for;
	bool attack;
	int hp;
	int totaldamage;
	std::list<std::unique_ptr<NPC_Opponent>> damagelist;

	Map *map;
	unsigned char index;
	unsigned char spawn_type;
	short spawn_time;
	unsigned char spawn_x, spawn_y;

	int id;

	bool pet;
	Character *PetOwner;
	bool PetFollowing = false;
	bool PetAttacking = false;
	bool PetGuarding = false;
	NPC *PetTarget = nullptr;
	int PetMinDamage;
	int PetMaxDamage;

	static void SetSpeedTable(std::array<double, 7> speeds);

	NPC(Map *map, short id, unsigned char x, unsigned char y, unsigned char spawn_type, short spawn_time, unsigned char index, bool temporary = false, bool Pet = false, Character *PetOwner = 0);

	const NPC_Data &Data() const;
	const ENF_Data &ENF() const;

	void Spawn(NPC *parent = 0);
	void Act();

	bool Walk(Direction);
	void Damage(Character *from, int amount, int spell_id = -1);
	void RemoveFromView(Character *target);
	void Killed(Character *from, int amount, int spell_id = -1);
	void Die(bool show = true);

	void Attack(Character *target);

	void Say(const std::string &message);

	void FormulaVars(std::unordered_map<std::string, double> &vars, std::string prefix = "");

	void PetSetOwner(Character *character);
	void Pet(NPC *npc);
	void PetDamage(NPC *from, int amount, int spell_id = -1);
	void PetWalkTo(int x, int y);
	void PetDetermineDirection(int x, int y);
	void PetFindAltRoute(int target_x, int target_y);
	bool PetFindPath(int target_x, int target_y, std::vector<Direction> &path);
	NPC *PetFindNearbyEnemy();

	void ResetDirectionChangeFlag();
	bool HasChangedDirection() const;

	~NPC();

private:
	bool direction_changed = false; // Track if the direction has changed
};

inline int pet_dir_dx(Direction direction)
{
	switch (direction)
	{
	case DIRECTION_LEFT:
		return -1;
	case DIRECTION_RIGHT:
		return 1;
	default:
		return 0;
	}
}

inline int pet_dir_dy(Direction direction)
{
	switch (direction)
	{
	case DIRECTION_UP:
		return -1;
	case DIRECTION_DOWN:
		return 1;
	default:
		return 0;
	}
}

#endif // NPC_HPP_INCLUDED
