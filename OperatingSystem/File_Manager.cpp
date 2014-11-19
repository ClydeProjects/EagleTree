/*
 * sequential_writer.cpp
 *
 *  Created on: Aug 22, 2012
 *      Author: niv
 */

#include "../ssd.h"
//#include "../MTRand/mtrand.h"

using namespace ssd;

File_Manager::File_Manager(long min_LBA, long max_LBA, uint num_files_to_write, long max_file_size, ulong randseed)
	: Thread(), min_LBA(min_LBA), max_LBA(max_LBA),
	  num_files_to_write(num_files_to_write),
	  max_file_size(max_file_size),
	  num_free_pages(max_LBA - min_LBA + 1),
	  double_generator(randseed * 13),
	  random_number_generator(randseed),
	  num_pending_trims(0),
	  phase(WRITE_PHASE),
	  current_file(NULL),
	  file_id_generator(0)
{
	Address_Range free_range(min_LBA, max_LBA);
	free_ranges.push_front(free_range);
}

File_Manager::File_Manager()
	: Thread(), min_LBA(0), max_LBA(0),
	  num_files_to_write(1000),
	  max_file_size(0),
	  num_free_pages(max_LBA - min_LBA + 1),
	  double_generator(2361),
	  random_number_generator(3613616),
	  num_pending_trims(0),
	  phase(WRITE_PHASE),
	  current_file(NULL),
	  file_id_generator(0)
{}

File_Manager::~File_Manager() {
	//print_thread_stats();
	for (auto f : files_history) {
		if (f != NULL) {
			delete f;
		}
	}
}

void File_Manager::issue_first_IOs() {
	//print_thread_stats();
	write_next_file();
}

void File_Manager::handle_no_IOs_left() {
	if (phase == WRITE_PHASE) {
		handle_file_completion();
		run_deletion_routine();
	}
	bool trim_phase_just_started = phase == TRIM_PHASE && num_pending_trims > 0;
	if (!trim_phase_just_started) {
		assert(num_pending_trims == 0);
		phase = WRITE_PHASE;
		write_next_file();
	}
}

void File_Manager::handle_event_completion(Event* event) {
 	if (event->get_event_type() == TRIM) {
		num_pending_trims--;
	} else if (event->get_event_type() == WRITE) {
		//event->print();
		current_file->register_write_completion(event);
	}
}

void File_Manager::handle_file_completion() {
	assert(current_file->is_finished());
	current_file->finish(get_current_time());
	live_files.push_back(current_file);
}

void File_Manager::write_next_file() {
	assert(num_free_pages > 0); // deal with this problem later
	double death_probability = double_generator() / 4;
	int size = 1 + random_number_generator() % max_file_size;
	if (size > num_free_pages) {
		size = num_free_pages;
	}
	num_free_pages -= size;
	current_file = new File(size, death_probability, get_current_time(), file_id_generator++);
	//printf("Writing file  %d  of size  %d.   Num free pages down to %d \n", current_file->id, current_file->size, num_free_pages);
	files_history.push_back(current_file);

	int num_pages_written = 0;
	while (num_pages_written < size) {
		Address_Range current = assign_new_range(size - num_pages_written);
		//printf("  assigning range %d  %d\n", current.min, current.max);
		current_file->ranges_comprising_file.push_back(current);
		for (int i = current.min; i <= current.max; i++) {
			issue_write(i);
			num_pages_written++;
		}
	}
}

Address_Range File_Manager::assign_new_range(int num_pages_left_to_allocate) {
	assert(free_ranges.size() > 0);
	Address_Range range = free_ranges.front();
	free_ranges.pop_front();
	long num_pages_left_to_write = num_pages_left_to_allocate;
	if (num_pages_left_to_write < range.get_size()) {
		Address_Range sub_range = range.split(num_pages_left_to_write);
		assert(range.min <= range.max);
		free_ranges.push_front(range);
		return sub_range;
	} else {
		return range;
	}
}

void File_Manager::issue_write(long lba) {
	long size = ENABLE_TAGGING ? current_file->size : 1;
	Event* event = new Event(WRITE, lba, size, get_current_time());
	if (ENABLE_TAGGING) {
		event->set_tag(current_file->id);
	}
	submit(event);
}

void File_Manager::run_deletion_routine() {
	do {
		randomly_delete_files();
	} while (num_free_pages == 0);
}

// Iterates through every live file and randomly devices whether or not to trim it
// based on a randomly generated number
void File_Manager::randomly_delete_files() {
	for (uint i = 0; i < live_files.size(); ) {
		File* file = live_files[i];
		double random_num = double_generator();

		// only start deleting files once less than 10% of the address space is occupied
		bool delete_files = num_free_pages / (double)(max_LBA - min_LBA) < 0.1;

		if (delete_files && file->death_probability > random_num && file != live_files.back()) {
			delete_file(file);
			live_files.erase(live_files.begin() + i);
			phase = TRIM_PHASE;
		} else {
			i++;
		}
	}
}

void File_Manager::delete_file(File* victim) {
	if (PRINT_FILE_MANAGER_INFO) {
		printf("deleting file  %d\n", victim->id);
	}
	num_free_pages += victim->size;
	//printf("delete file %d. Num free files up to: %d\n", victim->id, num_free_pages);
	schedule_to_trim_file(victim);
	reclaim_file_space(victim);
	assert(free_ranges.size() > 0);
	victim->time_deleted = get_current_time();
}

// merges the
void File_Manager::schedule_to_trim_file(File* file) {
	num_pending_trims += file->size;
	deque<Address_Range> freed_ranges = file->ranges_comprising_file;
	for (uint i = 0; i < freed_ranges.size(); i++) {
		Address_Range range = freed_ranges[i];
		for (int j = range.min; j <= range.max; j++) {
			//addresses_to_trim.insert(j);
			Event* trim = new Event(TRIM, j, 1, get_current_time());
			submit(trim);
		}
	}
}

// merges two sorted vectors of address ranges into one sorted vector, while merging contiguous ranges
void File_Manager::reclaim_file_space(File* file) {
	deque<Address_Range> freed_ranges = file->ranges_comprising_file;
	deque<Address_Range> new_list;
	if (free_ranges.size() == 0) {
		free_ranges = freed_ranges;
		return;
	}
	uint i = 0, j = 0;
	//printf("has  %d  ranges\n", freed_ranges.size());

	int sum_free_pages = 0;
	for (auto i : freed_ranges) {
		sum_free_pages += i.get_size();
	}
	for (auto i : free_ranges) {
		sum_free_pages += i.get_size();
	}

	while (i < freed_ranges.size() || j < free_ranges.size()) {

		if (j == free_ranges.size()) {
			//printf("    pushing  %")
			new_list.push_back(freed_ranges[i]);
			i++;
			continue;
		}
		else if (i == freed_ranges.size()) {
			new_list.push_back(free_ranges[j]);
			j++;
			continue;
		}

		Address_Range& freed_range = freed_ranges[i];
		//printf("  freeing range  %d  %d\n", freed_range.min, freed_range.max);
		Address_Range& existing_range = free_ranges[j];

		if (existing_range.is_contiguously_followed_by(freed_range)) {
			freed_range.merge(existing_range);
			j++;
		}
		else if (existing_range.is_followed_by(freed_range)) {
			new_list.push_back(existing_range);
			j++;
		}
		else if(freed_range.is_contiguously_followed_by(existing_range)) {
			existing_range.merge(freed_range);
			i++;
		}
		else if (freed_range.is_followed_by(existing_range)) {
			new_list.push_back(freed_range);
			i++;
		}
		else {
			assert(false);
		}
	}
	free_ranges = new_list;

	int new_sum = 0;
	for (auto range : new_list) {
		new_sum += range.get_size();
	}
	assert(new_sum == sum_free_pages);

}

void File_Manager::print_thread_stats() {
	printf("free pages:\t%d\n", num_free_pages);
	double percentage_written = 1 - num_free_pages / (double)(max_LBA - min_LBA);
	printf("occupied percentage of pages:\t%f\n", percentage_written);
	printf("max_file_size:\t%d\n", max_file_size);
	printf("free ranges:\n");
	for (auto r : free_ranges) {
		printf("%d  %d\n", r.min, r.max);
	}
	int total_pages_written = 0;
	for (auto f : live_files) {
		printf("file %d: ", f->id);
		printf("\t%d ", f->size);
		printf("\t%f ", f->death_probability);
		printf("\t%d ", (long)f->time_created);
		printf("\t%d ", (long)f->time_finished_writing);
		printf("\t%d ", (long)f->time_deleted);
		printf("\n");
		total_pages_written += f->size;
	}
	printf("total pages written: %d \n", total_pages_written);
	printf("\n");
}

// ----------------- Address_Range ---------------------------

Address_Range::Address_Range(long min, long max)
	: min(min), max(max)
{
	assert( min <= max);
}

bool Address_Range::is_contiguously_followed_by(Address_Range other) const {
	return max + 1 == other.min;
}

bool Address_Range::is_followed_by(Address_Range other) const {
	return max < other.min;
}

void Address_Range::merge(Address_Range other) {
	if (min - 1 == other.max) {
		min = other.min;
	} else if (max + 1 == other.min) {
		max = other.max;
	} else {
		fprintf(stderr, "Error: cannot merge these two Address Ranges since they are not contiguous\n");
		throw;
	}
}

// ----------------- FILE ---------------------------

File_Manager::File::File(uint size, double death_probability, double creation_time, int  id)
	: death_probability(death_probability), time_created(creation_time), time_finished_writing(0),
	  time_deleted(0), size(size), id(id),
	  num_pages_written(0)
{
	if (PRINT_FILE_MANAGER_INFO) {
		printf("creating file: %d  %d   %f\n", id, size, death_probability);
	}
	assert(death_probability >= 0 && death_probability <= 1);
	assert(size > 0);
}

void File_Manager::File::finish(double time) {
	time_finished_writing = time;
	if (PRINT_FILE_MANAGER_INFO) {
		printf("finished with file  %d\n", id);
	}
	printf("file has %d pages, and was written on %d luns\n", size, on_how_many_luns_is_this_file.size());
}

bool File_Manager::File::is_finished() const {
	return size == num_pages_written;
}

void File_Manager::File::register_write_completion(Event* event) {
	//printf("num_pages_written:  %d\n", num_pages_written);
	pair<int,int> p = pair<int,int>(event->get_address().package,event->get_address().die);
	on_how_many_luns_is_this_file.insert(p);
	num_pages_written++;
}
