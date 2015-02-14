#include "building.h"
#include "resource.h"
#include "city.h" // Needed in Building::amount_built()
#include "window.h" // For debugmsg
#include "stringfunc.h" // Needed in lookup_building_category()
#include "player_city.h"  // For close()
#include <sstream>

// R defaults to RES_NULL, A defaults to 1
Recipe::Recipe(Resource R, int A)
{
  result = Resource_amount(R, A);
}

std::string Recipe::save_data()
{
  std::stringstream ret;

// Since some names are multi-word, use ! as a terminator
  ret << name << " ! ";
  ret << result.type << " " << result.amount << " ";
  ret << units_per_day << " ";
  ret << days_per_unit << " ";
  ret << std::endl;

  ret << resource_ingredients.size() << " ";
  for (int i = 0; i < resource_ingredients.size(); i++) {
    ret << resource_ingredients[i].type << " " <<
           resource_ingredients[i].amount << " ";
  }
  ret << std::endl;

  ret << mineral_ingredients.size() << " ";
  for (int i = 0; i < mineral_ingredients.size(); i++) {
    ret << mineral_ingredients[i].type << " " <<
           mineral_ingredients[i].amount << " ";
  }
  ret << std::endl;

  return ret.str();
}

bool Recipe::load_data(std::istream& data)
{
  name = "";

  std::string tmpstr;
  while (tmpstr != "!") {
    data >> tmpstr;
    if (tmpstr != "!") {
      if (!name.empty()) {
        name = name + " ";
      }
      name = name + tmpstr;
    }
  }

  int tmptype, tmpnum;
  data >> tmptype >> tmpnum;
  if (tmptype <= 0 || tmptype >= RES_MAX) {
    debugmsg("Recipe loaded result of type %d (range is 1 to %d).",
             tmptype, RES_MAX - 1);
    return false;
  }
  result = Resource_amount( Resource(tmptype), tmpnum );

  data >> units_per_day >> days_per_unit;

  resource_ingredients.clear();
  int num_res;
  data >> num_res;
  for (int i = 0; i < num_res; i++) {
    data >> tmptype >> tmpnum;
    if (tmptype <= 0 || tmptype >= RES_MAX) {
      debugmsg("Recipe loaded res ingredient of type %d (range is 1 to %d).",
               tmptype, RES_MAX - 1);
      return false;
    }
    resource_ingredients.push_back(Resource_amount(Resource(tmptype), tmpnum));
  }

  mineral_ingredients.clear();
  int num_min;
  data >> num_min;
  for (int i = 0; i < num_min; i++) {
    data >> tmptype >> tmpnum;
    if (tmptype <= 0 || tmptype >= MINERAL_MAX) {
      debugmsg("Recipe loaded min ingredient of type %d (range is 1 to %d).",
               tmptype, MINERAL_MAX - 1);
      return false;
    }
    mineral_ingredients.push_back( Mineral_amount( Mineral(tmptype), tmpnum ) );
  }

  return true;
}

std::string Recipe::get_name()
{
  if (!name.empty()) {
    return name;
  }

  Resource res = get_resource();
  return Resource_data[ res ]->name;
}

Resource Recipe::get_resource()
{
  return result.type;
}

Resource Recipe_amount::get_resource()
{
  return recipe.get_resource();
}

Building::Building()
{
  pos = Point(-1, -1);
  construction_left = 0;
  type = BUILD_NULL;
  workers = 0;
  field_output = 0;
  shaft_output = 0;
  hunter_level = 0;
  hunting_target = ANIMAL_NULL;
  hunting_action = ANIMAL_ACT_KILL;
}

Building::~Building()
{
}

std::string Building::save_data()
{
  std::stringstream ret;

  ret << int(type) << " ";
  ret << open << " ";
  ret << pos.x << " " << pos.y << " ";
  ret << construction_left << " ";
  ret << workers << " ";

  ret << field_output << " ";
  ret << crops_grown.size() << " ";
  for (int i = 0; i < crops_grown.size(); i++) {
    ret << crops_grown[i].type << " " << crops_grown[i].amount << " ";
  }
  ret << std::endl;

  ret << shaft_output << " ";
  ret << minerals_mined.size() << " ";
  for (int i = 0; i < minerals_mined.size(); i++) {
    ret << minerals_mined[i].type << " " << minerals_mined[i].amount << " ";
  }
  ret << std::endl;

  ret << hunter_level << " ";
  ret << hunting_target << " ";
  ret << hunting_action << std::endl;

  ret << build_queue.size() << " ";
  for (int i = 0; i < build_queue.size(); i++) {
    ret << build_queue[i].recipe.save_data() << " " << build_queue[i].amount <<
           " ";
  }
  ret << std::endl;

  return ret.str();
}

bool Building::load_data(std::istream& data)
{
  int tmptype;
  data >> tmptype;
  if (tmptype <= 0 || tmptype >= BUILD_MAX) {
    debugmsg("Building loaded type of %d (range is 1 to %d).",
             tmptype, BUILD_MAX - 1);
    return false;
  }
  type = Building_type(tmptype);

  data >> open >> pos.x >> pos.y >> construction_left >> workers;

  crops_grown.clear();
  data >> field_output;
  int num_crops;
  data >> num_crops;
  for (int i = 0; i < num_crops; i++) {
    int tmpcrop, crop_amt;
    data >> tmpcrop >> crop_amt;
    if (tmpcrop <= 0 || tmpcrop >= CROP_MAX) {
      debugmsg("Building loaded crop %d/%d; type %d (range is 1 to %d).",
               i, num_crops, tmpcrop, CROP_MAX - 1);
      return false;
    }
    crops_grown.push_back( Crop_amount( Crop(tmpcrop), crop_amt ) );
  }

  minerals_mined.clear();
  data >> shaft_output;
  int num_mins;
  data >> num_mins;
  for (int i = 0; i < num_mins; i++) {
    int tmpmin, min_amt;
    data >> tmpmin >> min_amt;
    if (tmpmin <= 0 || tmpmin >= MINERAL_MAX) {
      debugmsg("Building loaded mineral %d/%d; type %d (range is 1 to %d).",
               i, num_mins, tmpmin, MINERAL_MAX - 1);
      return false;
    }
    minerals_mined.push_back( Mineral_amount( Mineral(tmpmin), min_amt ) );
  }

  data >> hunter_level;
  int tmptarget, tmpaction;
  data >> tmptarget >> tmpaction;
  if (tmptarget >= ANIMAL_MAX) {
    debugmsg("Building loaded hunting target %d (range is 1 to %d).",
             tmptarget, ANIMAL_MAX - 1);
    return false;
  }
  hunting_target = Animal(tmptarget);
  if (tmpaction >= ANIMAL_ACT_MAX) {
    debugmsg("Building loaded hunting action %d (range is 1 to %d).",
             tmpaction, ANIMAL_ACT_MAX - 1);
    return false;
  }
  hunting_action = Animal_action(tmpaction);

  int queue_size;
  data >> queue_size;
  for (int i = 0; i < queue_size; i++) {
    Recipe tmp_recipe;
    if (!tmp_recipe.load_data(data)) {
      debugmsg("Building failed to load recipe %d/%d.", i, queue_size);
      return false;
    }
    int recipe_amt;
    data >> recipe_amt;
    build_queue.push_back( Recipe_amount( tmp_recipe, recipe_amt ) );
  }

  return true;
}

void Building::set_type(Building_type new_type)
{
  type = new_type;
  build_queue.clear();
  crops_grown.clear();
  minerals_mined.clear();
}

void Building::make_queued()
{
  Building_datum* datum = get_building_datum();
  if (!datum) {
    debugmsg("Building::make_queued() called get_building_datum() and got \
NULL!");
    return;
  }

  construction_left = datum->build_time;
}

void Building::close(City* city)
{
  if (!city) {
    debugmsg("%s called Building::close(NULL)!", get_name().c_str());
    return;
  }

  if (!open) {  // We're already closed!
    return;
  }

  open = false;
  Citizen_type cit_type = get_job_citizen_type();

// Handle any stuff specific to farms, mines, etc
  for (int i = 0; i < crops_grown.size(); i++) {
    if (crops_grown[i].amount != HIDDEN_RESOURCE) {
      crops_grown[i].amount = 0;
    }
  }

  for (int i = 0; i < minerals_mined.size(); i++) {
    if (minerals_mined[i].amount != HIDDEN_RESOURCE) {
      minerals_mined[i].amount = 0;
    }
  }

  if (city->is_player_city()) {
    Player_city* p_city = static_cast<Player_city*>(city);
    p_city->fire_citizens(cit_type, workers, this);
  }
} 

int Building::get_empty_fields()
{
  if (!produces_resource(RES_FARMING)) {
    return 0;
  }
  int max_fields = get_total_jobs();
  int fields_used = 0;
  for (int i = 0; i < crops_grown.size(); i++) {
    fields_used += crops_grown[i].amount;
  }
  return (max_fields - fields_used);
}

int Building::get_empty_shafts()
{
  if (!produces_resource(RES_MINING)) {
    return 0;
  }
  int max_shafts = get_total_jobs();
  int shafts_used = 0;
  for (int i = 0; i < minerals_mined.size(); i++) {
    if (minerals_mined[i].amount != HIDDEN_RESOURCE) {
      shafts_used += minerals_mined[i].amount;
    }
  }
  return (max_shafts - shafts_used);
}

int Building::get_max_hunt_prey()
{
  if (hunting_target == ANIMAL_NULL) {
    return 0;
  }
  Animal_datum* ani_dat = Animal_data[hunting_target];
  if (ani_dat->difficulty == 0) {
    debugmsg("Animal '%s' has difficulty 0!", ani_dat->name.c_str());
    return 0;
  }
  return (workers * hunter_level) / ani_dat->difficulty;
}
  
int Building::get_max_hunt_food()
{
  if (hunting_target == ANIMAL_NULL) {
    return 0;
  }
  Animal_datum* ani_dat = Animal_data[hunting_target];
  int num_caught = get_max_hunt_prey();
  int remainder = (workers * hunter_level) % ani_dat->difficulty;
  return num_caught * ani_dat->food_killed +
         (remainder * ani_dat->food_killed) / ani_dat->difficulty;
}

Building_datum* Building::get_building_datum()
{
  return Building_data[type];
}

std::string Building::get_name()
{
  return get_building_datum()->name;
}

// res defaults to RES_NULL
bool Building::produces_resource(Resource res)
{
  Building_datum* build_dat = get_building_datum();
  return build_dat->produces_resource(res);
}

// res defaults to RES_NULL
bool Building::builds_resource(Resource res)
{
  Building_datum* build_dat = get_building_datum();
  return build_dat->builds_resource(res);
}

  
int Building::amount_produced(Resource res)
{
  Building_datum* build_dat = get_building_datum();
  return build_dat->amount_produced(res);
}

int Building::amount_built(Resource res, City* city)
{
  if (!city) {
    return 0;
  }

  if (build_queue.empty()) {
    return 0;
  }
// We only need to check the first item in our build_queue.
  Recipe rec = build_queue[0].recipe;
  if (rec.get_resource() == res &&
      city->has_resources(rec.resource_ingredients) &&
      city->has_minerals (rec.mineral_ingredients )) {
    int ret = rec.result.amount;
    ret *= workers;
// Multiple by units_per_day or divide by days_per_unit.
    if (rec.units_per_day != 0) {
      ret *= rec.units_per_day;
    } else if (rec.days_per_unit != 0) {
      ret /= rec.days_per_unit;
    }
    return ret;
  }

  return 0;
}

int Building::livestock_space()
{
  return get_building_datum()->livestock_space;
}

// cit_type defaults to CIT_NULL
int Building::get_total_jobs(Citizen_type cit_type)
{
  return get_building_datum()->get_total_jobs(cit_type);
}

// cit_type defaults to CIT_NULL
int Building::get_available_jobs(Citizen_type cit_type)
{
  int total = get_total_jobs(cit_type);
  if (workers >= total) {
    return 0;
  }
  return total - workers;
}

// cit_type defaults to CIT_NULL
int Building::get_filled_jobs(Citizen_type cit_type)
{
// Ensure that we actually hire citizens of that type
  if (cit_type != CIT_NULL && get_total_jobs(cit_type) == 0) {
    return 0;
  }
  return workers;
}

Citizen_type Building::get_job_citizen_type()
{
  return get_building_datum()->jobs.type;
}

int Building::get_upkeep()
{
  return get_building_datum()->upkeep;
}

int Building::get_total_wages()
{
  return (workers * get_building_datum()->wages);
}

int Building::get_destroy_cost()
{
  Building_datum* build_dat = get_building_datum();
  if (build_dat) {
    return build_dat->destroy_cost;
  }
  return 0;
} 

int Building::get_reopen_cost()
{
  Building_datum* build_dat = get_building_datum();
  if (build_dat) {
    int cost = 0;
    for (int i = 0; cost == 0 && i < build_dat->build_costs.size(); i++) {
      if (build_dat->build_costs[i].type == RES_GOLD) {
        cost = build_dat->build_costs[i].amount / 10;
      }
    }
    return cost;
  }
  return 0;
}

std::map<Resource,int> Building::get_maintenance()
{
  std::map<Resource,int> ret;

  Building_datum* bd_data = get_building_datum();

  if (!bd_data) {  // Should never ever happen, but why not
    return ret;
  }

  for (int i = 0; i < bd_data->maintenance_cost.size(); i++) {
    Resource_amount res = bd_data->maintenance_cost[i];
    if (ret.count(res.type)) {
      ret[res.type] += res.amount;
    } else {
      ret[res.type] = res.amount;
    }
  }

  if (ret.count(RES_GOLD)) {
    ret[RES_GOLD] += bd_data->upkeep;
  } else {
    ret[RES_GOLD] = bd_data->upkeep;
  }

  return ret;
}

// cit_type defaults to CIT_NULL
int Building::get_housing(Citizen_type cit_type)
{
  return get_building_datum()->get_housing(cit_type);
}

Building_datum::Building_datum()
{
  uid = -1;
  plural = false;
  category = BUILDCAT_NULL; // i.e. is an Area-only building
  upkeep = 0;
  military_support = 0;
  livestock_space = 0;
  build_time = 0;
  destroy_cost = 0;
  unlockable = false;
}

Building_datum::~Building_datum()
{
}

std::string Building_datum::get_short_description()
{
  std::stringstream ret;

  ret << "<c=white>" << name << "<c=/>" << std::endl;

  ret << "<c=yellow>Build time: " << build_time << " days.<c=/>" << std::endl;
  if (!build_costs.empty()) {
    ret << "<c=yellow>Build cost: ";
    for (int i = 0; i < build_costs.size(); i++) {
      ret << Resource_data[ build_costs[i].type ]->name << " x " <<
             build_costs[i].amount;
      if (i + 1 < build_costs.size()) {
        ret << ", ";
      }
    }
    ret << "<c=/>" << std::endl;
  }

  if (!maintenance_cost.empty()) {
    ret << "<c=ltred>Maintenance cost: ";
    for (int i = 0; i < maintenance_cost.size(); i++) {
      ret << Resource_data[ maintenance_cost[i].type ]->name << " x " <<
             maintenance_cost[i].amount;
      if (i + 1 < maintenance_cost.size()) {
        ret << ", ";
      }
    }
    ret << "<c=/>" << std::endl;
  }

  if (!housing.empty()) {
    ret << "<c=brown>Houses ";
    for (int i = 0; i < housing.size(); i++) {
      std::string cit_name = citizen_type_name(housing[i].type, true);
      ret << housing[i].amount << " " << cit_name << "<c=/>" << std::endl;
    }
  }

  if (jobs.amount > 0) {
    ret << "<c=cyan>Employs " << jobs.amount << " " <<
           citizen_type_name(jobs.type) << "s.<c=/>" << std::endl;
    if (!production.empty()) {
      ret << "<c=ltgreen>Each worker produces: ";
      for (int i = 0; i < production.size(); i++) {
        ret << Resource_data[ production[i].type ]->name << " x " <<
               production[i].amount;
        if (i + 1 < production.size()) {
          ret << ", ";
        }
      }
      ret << "<c=/>" << std::endl;
    }
  }

  if (!recipes.empty()) {
    ret << "<c=ltblue>Constructs: ";
    for (int i = 0; i < recipes.size(); i++) {
      ret << Resource_data[ recipes[i].get_resource() ]->name;
      if (i + 1 < recipes.size()) {
        ret << ", ";
      }
    }
    ret << "<c=/>" << std::endl;
  }

  return ret.str();
}

// res defaults to RES_NULL
bool Building_datum::produces_resource(Resource res)
{
  if (res == RES_NULL) {
    return !(production.empty());
  }

  for (int i = 0; i < production.size(); i++) {
    if (production[i].type == res) {
      return true;
    }
  }
  return false;
}

// res defaults to RES_NULL
bool Building_datum::builds_resource(Resource res)
{
  if (res == RES_NULL) {
    return !(recipes.empty());
  }

  for (int i = 0; i < recipes.size(); i++) {
    if (recipes[i].result.type == res) {
      return true;
    }
  }
  return false;
}

int Building_datum::amount_produced(Resource res)
{
  int ret = 0;
  for (int i = 0; i < production.size(); i++) {
    if (production[i].type == res) {
      ret += production[i].amount;
    }
  }
  return ret;
}

// cit_type defaults to CIT_NULL
int Building_datum::get_total_jobs(Citizen_type cit_type)
{
  if (cit_type == CIT_NULL || cit_type == jobs.type) {
    return jobs.amount;
  }
  return 0;
}

// cit_type defaults to CIT_NULL
int Building_datum::get_housing(Citizen_type cit_type)
{
  int ret = 0;
  for (int i = 0; i < housing.size(); i++) {
    if (cit_type == CIT_NULL || cit_type == housing[i].type) {
      ret += housing[i].amount;
    }
  }
  return ret;
}

bool Building_datum::add_production(Resource type, int amount)
{
  if (amount == INFINITE_RESOURCE) {
    debugmsg("%s produces infinite %s - this is invalid.",
             name.c_str(), Resource_data[type]->name.c_str());
    return false;
  } else if (amount == 0) {
    debugmsg("%s produces zero %s - this is invalid.",
             name.c_str(), Resource_data[type]->name.c_str());
    return false;
  } else if (amount < 0) {
    debugmsg("%s produces negative %s - this is invalid.",
             name.c_str(), Resource_data[type]->name.c_str());
    return false;
  }

  production.push_back( Resource_amount(type, amount) );
  return true;
}

Building_category lookup_building_category(std::string name)
{
  name = no_caps( trim( name ) );
  for (int i = 0; i < BUILDCAT_MAX; i++) {
    Building_category ret = Building_category(i);
    if (no_caps( trim( building_category_name(ret) ) ) == name) {
      return ret;
    }
  }
  return BUILDCAT_NULL;
}

std::string building_category_name(Building_category category)
{
  switch (category) {
    case BUILDCAT_NULL:           return "NULL";
    case BUILDCAT_MANUFACTURING:  return "manufacturing";
    case BUILDCAT_MAX:            return "BUG - BUILDCAT_MAX";
    default:  return "Unnamed Building_category";
  }
  return "BUG - Escaped building_category_name() switch";
}
