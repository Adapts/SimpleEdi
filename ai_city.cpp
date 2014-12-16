#include "ai_city.h"
#include "window.h" // For debugmsg()
#include "world.h"  // Needed for randomize_properties()
#include "rng.h"
#include <sstream>

AI_city::AI_city()
{
  type = CITY_TYPE_CITY;
  race = RACE_NULL;
  role = CITY_ROLE_NULL;
  radius = 1;
  free_peasants = 0;
  for (int i = 0; i < CIT_MAX; i++) {
    population[i].type = Citizen_type(i);
  }
}

AI_city::~AI_city()
{
}

void AI_city::randomize_properties(World_map* world)
{
  if (!world) {
    debugmsg("AI_city::randomize_properties(NULL) called!");
    return;
  }

  Race_datum* race_dat = Race_data[race];

// Set up our populations
  for (int i = 0; i < CIT_MAX; i++) {
    population[i].reset();
  }

  int pop = rng(race_dat->city_size_min[type], race_dat->city_size_max[type]);

// Figure out how to arrange pop, using the ratios from our race
/* Quick explanation of the equations below:
 * Let X = peasant pop, Y = merchant pop, Z = burgher pop, N = total pop
 * Let a = peasant:merchant ratio, b = merchant:burgher ratio.
 * Thus X = aY, Y = bZ, and N = X + Y + Z
 * Z = (N - Z) / (a * b)                    ===>   Z = N / (a * b + 1)
 * Y = X / a   ===>   Y = (N - Y - Z) / a   ===>   Y = (N - Z) / (a + 1)
 * X = N - Y - Z
 */

  int ratio_a = race_dat->citizen_ratio[CIT_MERCHANT],
      ratio_b = race_dat->citizen_ratio[CIT_BURGHER];
  if (ratio_a == 0 || ratio_b == 0) {
    debugmsg("Bad citizen_ratio in %s data!", race_dat->name.c_str());
    return;
  }
  int burghers  = pop / (1 + ratio_a * ratio_b);
  int merchants = (pop - burghers) / (1 + ratio_a);
  int peasants  = pop - merchants - burghers;

  population[CIT_PEASANT ].add_citizens(peasants);
  population[CIT_MERCHANT].add_citizens(merchants);
  population[CIT_BURGHER ].add_citizens(burghers);

  free_peasants = population[CIT_PEASANT].count;

// Figure out our radius, based on population.
  if (pop >= 10000) {
    radius = 3;
  } else if (pop >= 1000) {
    radius = 2;
  } else {
    radius = 1;
  }
  if (burghers >= 10) {
    radius += 2;
  } else if (burghers > 0) {
    radius += 1;
  }
  
// Now, pick a City_role from our terrain.
  Map_type mtype = world->get_map_type(location);
  Map_type_datum* map_dat = Map_type_data[mtype];

// Pick a role.  Only accept roles that our race can handle.
  std::vector<City_role> available_roles;
  for (int i = 0; i < map_dat->city_roles.size(); i++) {
    City_role_datum* role_dat = City_role_data[ map_dat->city_roles[i] ];
    Race_skill skill = role_dat->skill;
    int skill_req = role_dat->skill_req;
    if (skill != SKILL_NULL || race_dat->skill_level[skill] >= skill_req) {
      available_roles.push_back( map_dat->city_roles[i] );
    }
  }
  if (available_roles.empty()) {
    role = CITY_ROLE_NULL;  // daaaaarn
  } else {
    int index = rng(0, available_roles.size() - 1);
    role = available_roles[index];
  }

  setup_resource_production(world);
}

void AI_city::setup_resource_production(World_map* world)
{
  if (!world) {
    debugmsg("AI_city::setup_resource_production(NULL) called.");
    return;
  }

  resource_production.clear();
  mineral_production.clear();

// Data for our chosen role.
  City_role_datum* role_dat = City_role_data[role];

// A list of all tiles that are available for us to exploit.
  std::vector<Map_tile*> tiles;
  for (int x = 0 - radius; x <= radius; x++) {
    for (int y = 0 - radius; y <= radius; y++) {
      int mx = CITY_MAP_SIZE / 2 + x, my = CITY_MAP_SIZE / 2 + y;
      tiles.push_back( map.get_tile(mx, my) );
    }
  }

// Figure out the food that we need...
  int food_req = get_food_consumption();
/* We multiply by 50,000 to avoid rounding errors.  50,000 is the product of:
 * 100 - terrain farmability is a percentage (0 to 100)
 * 5 - Farming skill is measured from 0 to 5.
 * 100 - crop food output is per 100 units of the crop.
 * We also multiply by our chosen role's food_percentage, then divide by 100.
 * This is because some roles aim to produce more food than we require, while
 * others produce less and assume we'll import the deficit.
 */
  food_req = (food_req * 50000 * role_dat->food_percentage) / 100;
// TODO: If there's a nearby, friendly CITY_ROLE_FARMING city, decrease food_req


// Now try to match the required food.

// ** FARMING **
  int farm_skill = Race_data[race]->skill_level[SKILL_FARMING];
  if (farm_skill >= 2) {
    add_farms(tiles, food_req);
  } // if (farm_skill > 0)


  if (food_req > 0) { // Oh no, we still need food!
// We can get food from hunting or livestock.  Which are we better at?
    int hunting_skill   = Race_data[race]->skill_level[SKILL_HUNTING];
    int livestock_skill = Race_data[race]->skill_level[SKILL_LIVESTOCK];
    bool added_hunting_camps = false;

// Hunting is generally better than keeping livestock, so use it in a tie.
    if (hunting_skill >= 2 && hunting_skill >= livestock_skill) {
      added_hunting_camps = true;
      add_hunting_camps(tiles, food_req);
    }

// If we still need some food, add some livestock!
    if (food_req > 0 && livestock_skill >= 2) {
      add_pastures(tiles, food_req);
    }

// Finally, if we didn't add hunting camps on our first try, add them now.
    if (!added_hunting_camps && food_req > 0 && hunting_skill >= 2) {
      add_hunting_camps(tiles, food_req);
    }
  }

// Okay!  We are done with setting up food production.
// Now set up some production based on our City_role.
  switch (role) {
    case CITY_ROLE_FARMING:
      add_farms(tiles);
      break;
    case CITY_ROLE_HUNTING:
      add_hunting_camps(tiles);
      break;
    case CITY_ROLE_LIVESTOCK:
      add_pastures(tiles);
      break;
    case CITY_ROLE_MINING:
      add_mines(tiles);
      break;
    case CITY_ROLE_LOGGING:
      add_sawmills(tiles);
      break;
  } // switch (role)
}

int AI_city::get_net_food()
{
  return resource_production[RES_FOOD] - get_food_consumption();
}

std::string AI_city::list_production()
{
  std::stringstream ret;
  ret << "Type: " << City_role_data[role]->name << std::endl;
  for (std::map<Resource,int>::iterator it = resource_production.begin();
       it != resource_production.end();
       it++) {
    Resource res = it->first;
    int amount = it->second;
    ret << resource_name(res) << ": " << amount << std::endl;
  }
  for (std::map<Mineral,int>::iterator it = mineral_production.begin();
       it != mineral_production.end();
       it++) {
    Mineral min = it->first;
    int amount = it->second;
    ret << Mineral_data[min]->name << ": " << amount << std::endl;
  }
  return ret.str();
}

void AI_city::add_farms(std::vector<Map_tile*>& tiles, int& food_req)
{
  bool unlimited_food = (food_req == -1);
  int farm_skill = Race_data[race]->skill_level[SKILL_FARMING];
  int res_farming = 0;
  Building_datum* farm_dat = Building_data[BUILD_FARM];
  int num_workers = farm_dat->get_total_jobs(CIT_PEASANT);
  for (int i = 0; res_farming == 0 && i < farm_dat->production.size(); i++) {
    if (farm_dat->production[i].type == RES_FARMING) {
      res_farming = farm_dat->production[i].amount;
    }
  }
// Find the most food-producing tile.
  bool done = false;
  while (!done && !tiles.empty() && free_peasants > 0 &&
         (unlimited_food || food_req > 0)) {
    int best_food = 0, best_index = -1;
    for (int i = 0; i < tiles.size(); i++) {
      int food = tiles[i]->get_max_food_output();
      if (food > best_food) {
        best_food = food;
        best_index = i;
      }
    }
    if (best_index == -1) { // No tiles produce food!  At all!
      done = true;
    } else {
// Multiply by our race's farming skill, and res_farming from above.
      if (free_peasants < num_workers) {
        free_peasants = num_workers;
      }
      best_food = best_food * farm_skill * res_farming * num_workers;
      food_req -= best_food;
      free_peasants -= num_workers;
      add_resource_production(RES_FOOD, best_food / 50000);
      tiles.erase(tiles.begin() + best_index);
    }
  } // while (!done && food_req > 0 && !tiles.empty())
}

void AI_city::add_farms(std::vector<Map_tile*>& tiles)
{
  int temp = -1;
  add_farms(tiles, temp);
}

void AI_city::add_hunting_camps(std::vector<Map_tile*>& tiles, int& food_req)
{
  bool unlimited_food = (food_req == -1);
  int hunting_skill = Race_data[race]->skill_level[SKILL_HUNTING];
  int res_hunting = 0;
  Building_datum* camp_dat = Building_data[BUILD_HUNTING_CAMP];
  int num_workers = camp_dat->get_total_jobs(CIT_PEASANT);
  for (int i = 0; res_hunting == 0 && i < camp_dat->production.size(); i++) {
    if (camp_dat->production[i].type == RES_HUNTING) {
      res_hunting = camp_dat->production[i].amount;
    }
  }
// Find the most food-producing tile.
  bool done = false;
  while (!done && !tiles.empty() && free_peasants > 0 &&
         (unlimited_food || food_req > 0)) {
    int best_food = 0, best_index = -1;
    for (int i = 0; i < tiles.size(); i++) {
      int food = tiles[i]->get_max_hunting_output();
      if (food > best_food) {
        best_food = food;
        best_index = i;
      }
    }
    if (best_index == -1) { // No tiles produce food!  At all!
      done = true;
    } else {
// Multiply by our race's farming skill, and res_farming from above.
// Also multiply by 50,000 since food_req is multiplied by 50,000!  But also
// divide by 10 since we multiply by (5 + hunting_skill).
      if (free_peasants < num_workers) {
        free_peasants = num_workers;
      }
      best_food = (5000 * best_food * (5 + hunting_skill) * res_hunting *
                   num_workers);
      food_req -= best_food;
      free_peasants -= num_workers;
      add_resource_production(RES_FOOD, best_food / 50000);
      tiles.erase(tiles.begin() + best_index);
    }
  } // while (!done && food_req > 0 && !tiles.empty())
}

void AI_city::add_hunting_camps(std::vector<Map_tile*>& tiles)
{
  int temp = -1;
  add_hunting_camps(tiles, temp);
}

void AI_city::add_pastures(std::vector<Map_tile*>& tiles, int& food_req)
{
  bool unlimited_food = (food_req == -1);
  int livestock_skill = Race_data[race]->skill_level[SKILL_LIVESTOCK];
/* First, figure out the three most food-producing animals.
 * We look at how much food is produced in 50,000 days; this makes calculating
 * the food we can get by slaughtering animals easier.  It also makes our food
 * amounts work directly with food_req (since that's multiplied by 50,000).
 * If food_req is -1, then we're not just trying to produce food; other
 * resources are valuable too.  So pick ANY livestock animals.
 */
  std::vector<Animal> food_animals;
  std::vector<int> food_amounts;
  int min_food = 0;
  for (int i = 0; i < ANIMAL_MAX; i++) {
    Animal_datum* ani_dat = Animal_data[i];
// Only multiply food_livestock by 500 since it's measured per 100 animals
    int animal_food = ani_dat->food_livestock * 500;
// Now, food from slaughtering animals (divided by how long it takes to birth 1)
// We multiply by 50,000 to match our required food; then also multiply by 100
// since we have 100 animals (and thus need to divide reproduction_rate by 100)
    animal_food += (5000000 * ani_dat->food_killed) /
                   ani_dat->reproduction_rate;
// At livestock skill of 1, tameness must be >= 88; at skill of 5, >= 40
    if (ani_dat->tameness + 12 * livestock_skill >= 100 &&
        (unlimited_food ||
         (animal_food >= min_food || food_animals.size() < 3))) {
// Insert it in the proper place.
      bool inserted = false;
      for (int n = 0; !inserted && n < food_animals.size(); n++) {
        if (food_amounts[n] < animal_food) {
          inserted = true;
          food_animals.insert( food_animals.begin() + n, Animal(i)   );
          food_amounts.insert( food_amounts.begin() + n, animal_food );
        }
      }
      if (!inserted) { // Didn't find a place to put it; push it to the end.
        food_animals.push_back( Animal(i)   );
        food_amounts.push_back( animal_food );
      }
    }
// Clip our vector to the proper size - unless we're using unlimited food
    if (!unlimited_food) {
      while (food_animals.size() > 3) {
        food_animals.pop_back();
        food_amounts.pop_back();
        min_food = food_amounts.back();
      }
    }
  }

// Since all tiles support pastures equally, we just randomly pick some from all
// tiles that support pastures.
  std::vector<Map_tile*> pastures;
  std::vector<int> pasture_indices; // For deleting them from tiles
  for (int i = 0; i < tiles.size(); i++) {
    if (tiles[i]->can_build(AREA_PASTURE)) {
      pastures.push_back( tiles[i] );
      pasture_indices.push_back( i );
    }
  }
  while (!pastures.empty() && food_req > 0) {
    int index = rng(0, pastures.size());
    int orig_index = pasture_indices[index];
    pastures.erase( pastures.begin() + index );
    tiles.erase( tiles.begin() + orig_index );
// Pick an animal!
    int animal_index = rng(0, food_animals.size() - 1);
    Animal new_livestock = food_animals[ animal_index ];
    int food_amount = 100 * food_amounts[ animal_index ];
    food_req -= food_amount;
    add_resource_production(RES_FOOD, food_amount / 50000);

// Add resource production for anything else the animal may produce.
    Animal_datum* ani_dat = Animal_data[new_livestock];
    for (int i = 0; i < ani_dat->resources_livestock.size(); i++) {
      Resource_amount res_amt = ani_dat->resources_livestock[i];
      add_resource_production(res_amt);
    }
    for (int i = 0; i < ani_dat->resources_killed.size(); i++) {
      Resource_amount res_amt = ani_dat->resources_killed[i];
// Multiply by 100 since we have 100 animals birthing - effectively this
// divides reproduction_rate by 100.
      res_amt.amount = (res_amt.amount * 100) / ani_dat->reproduction_rate;
      if (res_amt.amount == 0) {
        res_amt.amount = 1;
      }
      add_resource_production(res_amt);
    }
    if (livestock.count(new_livestock)) {
      livestock[new_livestock] += 100;
    } else {
      livestock[new_livestock] = 100;
    }
  } // while (!pastures.empty() && food_req > 0)
}

void AI_city::add_pastures(std::vector<Map_tile*>& tiles)
{
  int temp = -1;
  add_pastures(tiles, temp);
}

void AI_city::add_mines(std::vector<Map_tile*>& tiles)
{
// TODO: Get info on how much a mine outputs per worker, and our race's skill
  Building_datum* mine_dat = Building_data[BUILD_MINE];
  int num_workers = mine_dat->get_total_jobs(CIT_PEASANT);
  for (int i = 0; free_peasants > 0 && i < tiles.size(); i++) {
    Map_tile* tile = tiles[i];
    if (tile->can_build(AREA_MINE)) {
// Remove the tile from availability.
      tiles.erase(tiles.begin() + i);
      i--;
      if (free_peasants < num_workers) {
        num_workers = free_peasants;
      }
      for (int n = 0; n < tile->minerals.size(); n++) {
        add_mineral_production(tile->minerals[n].type, num_workers);
      }
    }
  }
}

void AI_city::add_sawmills(std::vector<Map_tile*>& tiles)
{
// TODO: Get info on how much a sawmill outputs per worker, and our race's skill
  Building_datum* sawmill_dat = Building_data[BUILD_SAWMILL];
  int num_workers = sawmill_dat->get_total_jobs(CIT_PEASANT);
  int res_wood = 0;
  for (int i = 0; res_wood == 0 && i < sawmill_dat->production.size(); i++) {
    if (sawmill_dat->production[i].type == RES_LOGGING) {
      res_wood = sawmill_dat->production[i].amount;
    }
  }
  for (int i = 0; free_peasants > 0 && i < tiles.size(); i++) {
    Map_tile* tile = tiles[i];
    if (tile->can_build(AREA_SAWMILL) && tile->wood >= 3000) {
// Remove the tile from availability.
      tiles.erase(tiles.begin() + i);
      i--;
      if (free_peasants < num_workers) {
        num_workers = free_peasants;
      }
      add_resource_production(RES_WOOD, num_workers * res_wood);
    }
  }
}
  

void AI_city::add_resource_production(Resource_amount res_amt)
{
  add_resource_production(res_amt.type, res_amt.amount);
}

void AI_city::add_resource_production(Resource res, int amount)
{
  if (resource_production.count(res)) {
    resource_production[res] += amount;
  } else {
    resource_production[res] = amount;
  }
}

void AI_city::add_mineral_production(Mineral_amount min_amt)
{
  add_mineral_production(min_amt.type, min_amt.amount);
}

void AI_city::add_mineral_production(Mineral min, int amount)
{
  if (mineral_production.count(min)) {
    mineral_production[min] += amount;
  } else {
    mineral_production[min] = amount;
  }
}
