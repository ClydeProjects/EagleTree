/*
 * sequential_writer.cpp
 *
 *  Created on: Aug 22, 2012
 *      Author: niv
 */

#include "../ssd.h"
#include "../MTRand/mtrand.h"

using namespace ssd;

#define PRINT_FILE_INFO true

File_Manager::File_Manager(long min_LBA, long max_LBA, uint num_files_to_write, long max_file_size, double time_breaks, double start_time, ulong randseed)
	: Thread(start_time), min_LBA(min_LBA), max_LBA(max_LBA),
	  num_files_to_write(num_files_to_write), time_breaks(time_breaks),
	  max_file_size(max_file_size),
	  num_free_pages(max_LBA - min_LBA + 1),
	  double_generator(randseed * 13),
	  random_number_generator(randseed)
{
	Address_Range free_range(min_LBA, max_LBA);
	free_ranges.push_front(free_range);
	write_next_file(0);
}

File_Manager::~File_Manager() {
	for (uint i = 0; i < files_history.size(); i++) {
		delete files_history[i];
	}
}

Event* File_Manager::issue_next_io() {
	if (addresses_to_trim.size() > 0) {
		return issue_trim();
	} else if (!current_file->has_issued_last_io() && !current_file->needs_new_range()) {
		return issue_write();
	} else {
		return NULL;
	}
}

Event* File_Manager::issue_trim() {
	long lba = *addresses_to_trim.begin();
	addresses_to_trim.erase(lba);
	return new Event(TRIM, lba, 1, time);
}

Event* File_Manager::issue_write() {
	long lba = current_file->get_next_lba_to_be_written();
	long size = ENABLE_TAGGING ? current_file->size : 1;
	Event* event = new Event(WRITE, lba, size, time);
	if (ENABLE_TAGGING) {
		event->set_tag(current_file->id);
	}
	time += time_breaks;
	return event;
}

void File_Manager::handle_event_completion(Event*event) {
	if (event->get_event_type() == TRIM)
		return;
	//time_breaks += throughout_moderator.register_event_completion(*event);
	current_file->register_write_completion();
	if (current_file->is_finished()) {
		handle_file_completion(event->get_current_time());
		time = max(time, event->get_current_time());
	}
	else if (current_file->needs_new_range()) {
		assign_new_range();
		time = max(time, event->get_current_time());
	}
}

void File_Manager::handle_file_completion(double current_time) {
	current_file->finish(current_time);
	live_files.push_back(current_file);
	files_history.push_back(current_file);

	if (num_files_to_write-- == 0) {
		finished = true;
		return;
	}
	do {
		randomly_delete_files(current_time);
	} while (num_free_pages == 0);
	//StateVisualiser::print_page_status();
	StatisticsGatherer::get_instance()->print();
	write_next_file(current_time);
}

double File_Manager::generate_death_probability() {
	double death_probability = double_generator();
	if (death_probability < 0.5) {
		death_probability *= 3.0/4.0;
	} else {
		death_probability -= 0.5;
		death_probability *= 3.0/4.0;
		death_probability = 1 - death_probability;
	}
	return death_probability;
}

void File_Manager::write_next_file(double current_time) {
	assert(num_free_pages > 0); // deal with this problem later
	double death_probability = double_generator() / 4;
	//double death_probability = generate_death_probability();
	uint size = 1 + random_number_generator() % max_file_size;
	if (size > num_free_pages) {
		size = num_free_pages;
	}
	num_free_pages -= size;
	current_file = new File(size, death_probability, current_time);
	assign_new_range();
}

void File_Manager::assign_new_range() {
	Address_Range range = free_ranges.front();
	free_ranges.pop_front();
	long num_pages_left_to_write = current_file->get_num_pages_left_to_allocate();
	if (num_pages_left_to_write < range.get_size()) {
		Address_Range sub_range = range.split(num_pages_left_to_write);
		free_ranges.push_front(range);
		current_file->register_new_range(sub_range);
	} else {
		current_file->register_new_range(range);
	}
}

void File_Manager::randomly_delete_files(double current_time) {
	for (uint i = 0; i < live_files.size(); ) {
		File* file = live_files[i];
		double random_num = double_generator();
		if (file->death_probability > random_num) {
			delete_file(file, current_time);
			live_files.erase(live_files.begin() + i);
		} else {
			i++;
		}
	}
}

void File_Manager::delete_file(File* victim, double current_time) {
	if (PRINT_FILE_INFO) {
		printf("deleting file  %d\n", victim->id);
	}
	num_free_pages += victim->size;
	schedule_to_trim_file(victim);
	reclaim_file_space(victim);
	victim->time_deleted = current_time;
}

// merges the
void File_Manager::schedule_to_trim_file(File* file) {
	deque<Address_Range> freed_ranges = file->ranges_comprising_file;
	for (uint i = 0; i < freed_ranges.size(); i++) {
		Address_Range range = freed_ranges[i];
		for (uint j = range.min; j <= range.max; j++) {
			addresses_to_trim.insert(j);
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
	while (i < freed_ranges.size() || j < free_ranges.size()) {
		Address_Range& freed_range = freed_ranges[i];
		Address_Range& existing_range = free_ranges[j];
		if (j == free_ranges.size()) {
			new_list.push_back(freed_range);
			i++;
		}
		else if (i == freed_ranges.size()) {
			new_list.push_back(existing_range);
			j++;
		}
		else if (existing_range.is_contiguously_followed_by(freed_range)) {
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
}

void File_Manager::print_thread_stats() {
	int total_pages_written = 0;
	for (uint i = 0; i < files_history.size(); i++) {
		File* f = files_history[i];
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

File_Manager::Address_Range::Address_Range(long min, long max)
	: min(min), max(max)
{
	assert( min <= max);
}

bool File_Manager::Address_Range::is_contiguously_followed_by(Address_Range other) const {
	return max + 1 == other.min;
}

bool File_Manager::Address_Range::is_followed_by(Address_Range other) const {
	return max < other.min;
}

void File_Manager::Address_Range::merge(Address_Range other) {
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

int File_Manager::File::file_id_generator = 0;

File_Manager::File::File(uint size, double death_probability, double creation_time)
	: death_probability(death_probability), time_created(creation_time), time_finished_writing(0),
	  time_deleted(0), size(size), id(file_id_generator++),
	  num_pages_written(0), current_range_being_written(-1, -1), num_pages_allocated_so_far(0)
{
	if (PRINT_FILE_INFO) {
		printf("creating file: %d  %d   %f\n", id, size, death_probability);
	}
	assert(death_probability >= 0 && death_probability <= 1);
	assert(size > 0);
}

void File_Manager::File::finish(double time) {
	time_finished_writing = time;
	ranges_comprising_file.push_back(current_range_being_written);
	if (PRINT_FILE_INFO) {
		printf("finished with file  %d\n", id);
	}
}

bool File_Manager::File::is_finished() const {
	return size == num_pages_written;
}

void File_Manager::File::register_write_completion() {
	num_pages_written++;
}

bool File_Manager::File::needs_new_range() const {
	return logical_addresses_to_be_written_in_current_range.size() == 0 && num_pages_allocated_so_far < size;
}

bool File_Manager::File::has_issued_last_io() {
	return logical_addresses_to_be_written_in_current_range.size() == 0 && num_pages_allocated_so_far == size;
}

long File_Manager::File::get_next_lba_to_be_written() {
	assert(logical_addresses_to_be_written_in_current_range.size() > 0);
	long lba = *logical_addresses_to_be_written_in_current_range.begin();
	logical_addresses_to_be_written_in_current_range.erase(lba);
	return lba;
}

long File_Manager::File::get_num_pages_left_to_allocate() const {
	return size - num_pages_allocated_so_far;
}

void File_Manager::File::register_new_range(Address_Range range) {
	if (PRINT_FILE_INFO) {
		printf("new range for file: %d    (%d - %d)  in total: %d\n", id, range.min, range.max, range.get_size());
	}
	assert(logical_addresses_to_be_written_in_current_range.size() == 0);
	if (num_pages_written > 0) {
		ranges_comprising_file.push_back(current_range_being_written);
		assert(range.max > current_range_being_written.min);
	}
	current_range_being_written = range;
	num_pages_allocated_so_far += range.get_size();
	assert(num_pages_allocated_so_far <= size);
	for (int i = range.min; i <= range.max; i++) {
		logical_addresses_to_be_written_in_current_range.insert(i);
	}
}
