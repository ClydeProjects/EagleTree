/*
 * wear_leveling_strategy.cpp
 *
 *  Created on: Feb 14, 2013
 *      Author: niv
 */

#include "../ssd.h"

using namespace ssd;
using namespace std;

Wear_Leveling_Strategy::Wear_Leveling_Strategy()
	: ssd(NULL),
	  migrator(NULL),
	  num_erases_up_to_date(0),
	  average_erase_cycle_time(0),
	  max_age(1),
	  age_distribution(),
	  all_blocks(),
	  random_number_generator(90),
	  block_data(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE, Block_data()) {

}

Wear_Leveling_Strategy::Wear_Leveling_Strategy(Ssd* ssd, Migrator* migrator)
	: ssd(ssd),
	  migrator(migrator),
	  num_erases_up_to_date(0),
	  average_erase_cycle_time(0),
	  max_age(1),
	  age_distribution(),
	  all_blocks(),
	  random_number_generator(90),
	  block_data(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE, Block_data())
{
	init();
}

void Wear_Leveling_Strategy::init() {
	age_distribution[0] = NUMBER_OF_ADDRESSABLE_BLOCKS();
	for (uint i = 0; i < SSD_SIZE; i++) {
			Package* package = ssd->get_package(i);
			for (uint j = 0; j < PACKAGE_SIZE; j++) {
				Die* die = package->get_die(j);
				for (uint t = 0; t < DIE_SIZE; t++) {
					Plane* plane = die->get_plane(t);
					for (uint b = 0; b < PLANE_SIZE; b++) {
						Block* block = plane->get_block(b);
						all_blocks.push_back(block);
					}
				}
			}
		}
}

double Wear_Leveling_Strategy::get_min_age() const {
	map<int, int>::const_iterator it = age_distribution.begin();
	int age = (*it).first;
	return age;
}

double Wear_Leveling_Strategy::get_normalised_age(uint age) const {
	double min_age = get_min_age();
	double normalized_age = (age - min_age) / (max_age - min_age);
	assert(normalized_age >= 0 && normalized_age <= 1);
	return normalized_age;
}

// TODO, at erase registration, there should be a check for WL queue. If not empty, see if can issue a WL operation. If cannot, issue an emergency GC.
// if the queue is empty, check if should trigger GC.
void Wear_Leveling_Strategy::register_erase_completion(Event const& event) {
	num_erases_up_to_date++;
	Address pba = event.get_address();
	Block* b = ssd->get_package(pba.package)->get_die(pba.die)->get_plane(pba.plane)->get_block(pba.block);

	int id = pba.get_block_id();
	Block_data& data = block_data[id];
	data.age++;
	assert(data.age == b->get_age());

	if (data.age > max_age) {
		max_age = data.age;
	}

	double time_since_last_erase = event.get_current_time() - data.last_erase_time;
	data.last_erase_time = event.get_current_time();

	average_erase_cycle_time = average_erase_cycle_time * 0.8 + 0.2 * time_since_last_erase;
	int age = data.age - 1;
	if (age_distribution.count(age - 1) == 1) {
		age_distribution[age - 1]--;
		age_distribution[age]++;
		if (age_distribution[age - 1] == 0) {
			age_distribution.erase(age - 1);
		}
	}

	if (blocks_being_wl.count(b) > 0) {
		blocks_being_wl.erase(b);
		StatisticsGatherer::get_global_instance()->print();
	}

	// looking for gc candidates every single time to update the list. This is CPU expensive, so we want to make it more seldom in the future.
	if (ENABLE_WEAR_LEVELING /* blocks_to_wl.size() == 0 */ && blocks_being_wl.size() < MAX_ONGOING_WL_OPS && max_age > get_min_age() + WEAR_LEVEL_THRESHOLD) {
		blocks_to_wl.clear();
		find_wl_candidates(event.get_current_time());
		//StateVisualiser::print_block_ages();
		//StatisticsGatherer::get_instance()->print();
	}

	if (blocks_to_wl.size() > 0 && blocks_being_wl.size() < MAX_ONGOING_WL_OPS) {
		int random_index = random_number_generator() % blocks_to_wl.size();
		set<Block*>::iterator i = blocks_to_wl.begin();
		advance(i, random_index);
		Block* target = *i;
		Address addr = Address(target->get_physical_address(), BLOCK);
		if (PRINT_LEVEL > 1) {
			printf("Scheduling WL in "); addr.print(); printf("\n");
		}
		migrator->schedule_gc(event.get_current_time(), addr.package, addr.die, addr.block, -1);
	}
}

bool Wear_Leveling_Strategy::schedule_wear_leveling_op(Block* victim) {
	if (blocks_being_wl.size() >= MAX_ONGOING_WL_OPS) {
		return false;
	} else {
		blocks_being_wl.insert(victim);
	}
	blocks_to_wl.erase(victim);
}

/*void Wear_Leveling_Strategy::update_blocks_with_min_age(uint min_age) {
	for (uint i = 0; i < all_blocks.size(); i++) {
		Block* b = all_blocks[i];
		uint age_ith_block = BLOCK_ERASES - b->get_erases_remaining();
		if (age_ith_block == min_age) {
			blocks_with_min_age.insert(all_blocks[i]);
		}
	}
}*/

void Wear_Leveling_Strategy::find_wl_candidates(double current_time) {
	for (uint i = 0; i < all_blocks.size(); i++) {
		Block* b = all_blocks[i];
		Block_data& data = block_data[i];
		double normalised_age = get_normalised_age(data.age);
		double time_since_last_erase = current_time - data.last_erase_time;
		if (b->get_state() == ACTIVE && normalised_age < 0.1 && time_since_last_erase  > average_erase_cycle_time * 10) {
			blocks_to_wl.insert(b);
		}
	}
	if (PRINT_LEVEL > 1) {
		printf("%d elements inserted into WL queue\n", blocks_to_wl.size());
	}
}


