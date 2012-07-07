
#include "ssd.h"

using namespace ssd;
using namespace std;

Block_manager_parallel_wearwolf_locality::Block_manager_parallel_wearwolf_locality(Ssd& ssd, FtlParent& ftl)
	: Block_manager_parallel_wearwolf(ssd, ftl)
{
}

Block_manager_parallel_wearwolf_locality::~Block_manager_parallel_wearwolf_locality(void) {}

Block_manager_parallel_wearwolf_locality::sequential_writes_tracking::sequential_writes_tracking()
	: counter(0),
	  last_LBA_timestamp(0.0),
	  pointers(NULL)
{}

Block_manager_parallel_wearwolf_locality::sequential_writes_tracking::~sequential_writes_tracking() {
	delete pointers;
}

Address Block_manager_parallel_wearwolf_locality::choose_write_location(Event const& event) {
	// check if write matches sequential pattern
	logical_address lb = event.get_logical_address();
	Address to_return;

	if (sequential_writes_key_lookup.count(lb) > 0) {
		logical_address key = sequential_writes_key_lookup[lb];
		sequential_writes_tracking& seq_write_metadata = *sequential_writes_identification_and_data[key];
		seq_write_metadata.counter++;

		logical_address start_address = sequential_writes_key_lookup[lb];
		sequential_writes_key_lookup.erase(lb);
		sequential_writes_key_lookup[lb + 1] = start_address;

		if (seq_write_metadata.pointers != NULL) {
			std::vector<std::vector<Address> >& pointers = *seq_write_metadata.pointers;
			pair<uint, uint> best_die_id = Block_manager_parent::get_free_die_with_shortest_IO_queue(pointers);

			// update metadata
			Address best_die = to_return = pointers[best_die_id.first][best_die_id.second];
			best_die.page++;
			pointers[best_die_id.first][best_die_id.second] = best_die;

			seq_write_metadata.last_LBA_timestamp = event.get_start_time();

			sequential_writes_key_lookup.erase(lb);
			sequential_writes_key_lookup[lb + 1] = key;
		} else if (seq_write_metadata.counter == 3) {
			set_pointers_for_sequential_write(seq_write_metadata);

		} else {
			to_return = Block_manager_parallel_wearwolf::choose_write_location(event);
		}
	} else {
		sequential_writes_key_lookup[lb + 1] = lb;
		sequential_writes_identification_and_data[lb] = new sequential_writes_tracking();
		to_return = Block_manager_parallel_wearwolf::choose_write_location(event);
	}

	return to_return;
}

void Block_manager_parallel_wearwolf_locality::set_pointers_for_sequential_write(sequential_writes_tracking swt) {

}
