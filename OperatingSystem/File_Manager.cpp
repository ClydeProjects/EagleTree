/*
 * sequential_writer.cpp
 *
 *  Created on: Aug 22, 2012
 *      Author: niv
 */

#include "../ssd.h"
#include "../MTRand/mtrand.h"

using namespace ssd;

File_Manager::File_Manager(long min_LBA, long max_LBA, uint num_files_to_write, long max_file_size, double time_breaks, double start_time, ulong randseed)
	: Thread(start_time), min_LBA(min_LBA), max_LBA(max_LBA),
	  num_files_to_write(num_files_to_write), time_breaks(time_breaks),
	  max_file_size(max_file_size)
{
	random_number_generator.seed(randseed);
	double_generator.seed(randseed * 17);
	Address_Range free_range(min_LBA, max_LBA);
	free_ranges.push_front(free_range);
	num_free_pages = max_LBA - min_LBA + 1;
	write_next_file();
}

Event* File_Manager::issue_next_io() {
	if (addresses_to_trim.size() > 0) {
		return issue_trim();
	} else if (!current_file->has_issued_last_io()) {
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
	Event* event = new Event(WRITE, lba, 1, time);
	time += time_breaks;
	return event;
}

void File_Manager::handle_event_completion(Event*event) {
	if (event->get_event_type() == TRIM) {
		return;
	}
	current_file->register_write_completion();
	if (current_file->is_finished()) {
		current_file->finish(event->get_current_time());
		files.push_back(current_file);
		time = max(time, event->get_current_time());
		if (num_files_to_write-- == 0) {
			finished = true;
		}
		else {
			delete_some_file();
			write_next_file();
		}
	}
	else if (current_file->needs_new_range()) {
		assign_new_range();
	}

}

void File_Manager::write_next_file() {
	assert(num_free_pages > 0); // deal with this problem later
	double death_probability = double_generator();
	uint size;
	if (max_file_size > num_free_pages) {
		size = num_free_pages;
	} else {
		size = 1 + random_number_generator() % max_file_size; // max file size
	}
	num_free_pages -= size;
	current_file = new File(size, death_probability);
	assign_new_range();
}

void File_Manager::assign_new_range() {
	Address_Range range = free_ranges.front();
	free_ranges.pop_front();
	long num_pages_left_to_write = current_file->get_num_pages_left_to_write();
	if (num_pages_left_to_write < range.get_size()) {
		Address_Range sub_range = range.split(num_pages_left_to_write);
		free_ranges.push_front(range);
		current_file->register_new_range(sub_range);
	} else {
		current_file->register_new_range(range);
	}
}

void File_Manager::delete_some_file() {
	for (uint i = 0; i < files.size(); ) {
		File* file = files[i];
		double random_num = double_generator();
		if (file->death_probability < random_num) {
			delete_file(file);
			files.erase(files.begin() + i);
		} else {
			i++;
		}
	}
}

void File_Manager::delete_file(File* victim) {
	printf("deleting file  %d\n", victim->file_id);
	num_free_pages += victim->size;
	schedule_to_trim_file(victim);
	reclaim_file_space(victim);
	delete victim;
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
	if (file->file_id == 5) {
		int i = 0;
		i++;
	}
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

File_Manager::File::File(uint size, double death_probability)
	: death_probability(death_probability), size(size), file_id(file_id_generator++),
	  num_pages_written(0), current_range_being_written(-1, -1), num_pages_allocated_so_far(0)
{
	printf("creating file: %d  %d   %f\n", file_id, size, death_probability);
	assert(death_probability >= 0 && death_probability <= 1);
	assert(size > 0);
}

void File_Manager::File::finish(double time) {
	time_finished = time;
	ranges_comprising_file.push_back(current_range_being_written);
	printf("finished with file  %d\n", file_id);
	if (file_id == 2) {
		int i = 0;
		i++;
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
	long lba = *logical_addresses_to_be_written_in_current_range.begin();
	logical_addresses_to_be_written_in_current_range.erase(lba);
	return lba;
}

long File_Manager::File::get_num_pages_left_to_write() const {
	return size - num_pages_written;
}

void File_Manager::File::register_new_range(Address_Range range) {
	printf("new range for file: %d    (%d - %d)\n", file_id, range.min, range.max);
	assert(logical_addresses_to_be_written_in_current_range.size() == 0);
	if (num_pages_written > 0) {
		ranges_comprising_file.push_back(current_range_being_written);
		assert(range.max > current_range_being_written.min);
	}
	current_range_being_written = range;
	num_pages_allocated_so_far += range.get_size();
	for (int i = range.min; i <= range.max; i++) {
		logical_addresses_to_be_written_in_current_range.insert(i);
	}
}
