/*
 * grace_hash_join.cpp
 *
 *  Created on: Nov 5, 2012
 *      Author: mks
 */

#include "../ssd.h"
//#include "../MTRand/mtrand.h"

using namespace ssd;

int Grace_Hash_Join::grace_counter = 0;

Grace_Hash_Join::Grace_Hash_Join
       (long relation_A_min_LBA, long relation_A_max_LBA,
		long relation_B_min_LBA, long relation_B_max_LBA,
		long free_space_min_LBA, long free_space_max_LBA,
		bool use_flexible_reads, bool use_tagging,
		long rows_per_page,      int randseed) :
		Thread(),
		relation_A_min_LBA(relation_A_min_LBA), relation_A_max_LBA(relation_A_max_LBA),
		relation_B_min_LBA(relation_B_min_LBA), relation_B_max_LBA(relation_B_max_LBA),
		free_space_min_LBA(free_space_min_LBA), free_space_max_LBA(free_space_max_LBA),
        use_flexible_reads(use_flexible_reads), use_tagging(use_tagging),
		input_cursor(relation_A_min_LBA),
		rows_per_page(rows_per_page),
		phase(BUILD),
        flex_reader(NULL),
		random_number_generator(randseed),
		victim_buffer(UNDEFINED),
        small_bucket_begin(0), small_bucket_end(0),
        large_bucket_begin(0), large_bucket_cursor(0), large_bucket_end(0),
        reads_in_progress_set(),
        writes_in_progress(0),
        reads_in_progress(0),
        finished_reading_smaller_bucket(false),
        finished_trimming_smaller_bucket(false)
{
	assert(relation_A_min_LBA < relation_A_max_LBA);
	assert(relation_B_min_LBA < relation_B_max_LBA);
	assert(free_space_min_LBA < free_space_max_LBA);

	relation_A_size = (relation_A_max_LBA - relation_A_min_LBA);
	relation_B_size = (relation_B_max_LBA - relation_B_min_LBA);
	free_space_size = (free_space_max_LBA - free_space_min_LBA);

	assert(free_space_size >= relation_A_size + relation_B_size);

	long larger_relation_size = max(relation_A_size, relation_B_size);
	num_partitions = floor(sqrt(larger_relation_size));
	partition_size = free_space_size / num_partitions;

	output_buffers.resize(num_partitions, 0);
	for (int i = 0; i < num_partitions; i++) {
		output_cursors.push_back(free_space_min_LBA + (int) (free_space_size * ((double) i / (num_partitions + 1))));
	}
	output_cursors_startpoints = vector<int>(output_cursors);
}

void Grace_Hash_Join::create_flexible_reader(int start, int finish) {
	if (flex_reader != NULL) delete flex_reader;
	vector<Address_Range> ranges;
	//printf("(A) Range: %d -> %d\n", small_bucket_cursor, small_bucket_end);
	ranges.push_back(Address_Range(start, finish));
	assert(os != NULL);
	flex_reader = os->create_flexible_reader(ranges);
}

void Grace_Hash_Join::issue_first_IOs() {
	if (phase == BUILD) {
		execute_build_phase();
	}
}

void Grace_Hash_Join::handle_event_completion(Event* event) {
	if (phase == PROBE_SYNCH && event->get_event_type() != TRIM) {
		Event* trim = new Event(TRIM, event->get_logical_address(), 1, get_current_time());
		submit(trim);
	}

	if (victim_buffer == num_partitions - 1 && reads_in_progress == 0) {
		phase = DONE;
		//finished = true;
		grace_counter++;
	}

	if (event->get_event_type() == READ_TRANSFER) {
		reads_in_progress_set.erase(event->get_logical_address());
		reads_in_progress--;
	} else if (event->get_event_type() == WRITE) {
		writes_in_progress--;
	}

	if (event->get_event_type() != WRITE) {
		if (phase == BUILD) {
			handle_read_completion_build();
			execute_build_phase();
		} else if (phase == PROBE_SYNCH || phase == PROBE_ASYNCH) {
			execute_probe_phase();
		}
	}
}

void Grace_Hash_Join::execute_build_phase() {
	//for (uint i = 0; i < output_buffers.size(); i++) cout << " " << output_buffers[i] << "|"; cout << "\n"; // Printout of buffer utilization

	// Only one read at the same time = synchronous reads
	if (reads_in_progress > 0) return;

	// The flexible reader initialization cannot be done in the constructor, since the OS object is not set at that point yet
	if (use_flexible_reads && input_cursor == relation_A_min_LBA) {
		create_flexible_reader(relation_A_min_LBA, relation_A_max_LBA);
		//printf("get_num_reads_left: %d\n", flex_reader->get_num_reads_left());
	}

	//bool done_reading = (input_cursor == relation_A_max_LBA + 1 || input_cursor == relation_B_max_LBA + 1);
	bool done_reading_relation_A = input_cursor == relation_A_max_LBA + 1;
	bool done_reading_relation_B = input_cursor == relation_B_max_LBA + 1;

	if ((done_reading_relation_A || done_reading_relation_B) && writes_in_progress != 0) {
		return;
	}
	else if (done_reading_relation_A) {
		if (PRINT_LEVEL >= 1) printf("Grace Hash Join, build phase: Switching to relation 2\n");
		input_cursor = relation_B_min_LBA;
		//VisualTracer::get_instance()->print_horizontally(6000);
		output_cursors_splitpoints = vector<int>(output_cursors);
		if (use_flexible_reads) {
			assert(flex_reader->is_finished());
			create_flexible_reader(relation_B_min_LBA, relation_B_max_LBA);
		}
		//VisualTracer::get_instance()->print_horizontally(6000);
	}
	else if (done_reading_relation_B) {
		phase = PROBE_ASYNCH;
		input_cursor = free_space_min_LBA;
		victim_buffer = UNDEFINED;
		//VisualTracer::get_instance()->print_horizontally(6000);
		execute_probe_phase();
		return;
	}

		// Read new content to input buffer
		reads_in_progress_set.insert(input_cursor);
		reads_in_progress++;
		double time = get_current_time();
		Event* event = use_flexible_reads ? flex_reader->read_next(time) : new Event(READ, input_cursor, 1, time);
		input_cursor++;
		submit(event);
}

void Grace_Hash_Join::handle_read_completion_build() {
	//put the records into an appropriate bucket
	for (int i = 0; i < rows_per_page; i++) {
		int bucket_index = random_number_generator() % num_partitions;
		int num_items = output_buffers[bucket_index]++;
		if (num_items == rows_per_page)
			flush_buffer(bucket_index);
	}

	// If we finished doing all the reads in the build phase, flush all the buckets
	bool done_reading = (input_cursor == relation_A_max_LBA + 1 || input_cursor == relation_B_max_LBA + 1);
	for (uint i = 0; done_reading && i < output_buffers.size(); i++)
		if (output_buffers[i] > 0)
			flush_buffer(i);
}

void Grace_Hash_Join::flush_buffer(int buffer_id) {
	bool at_relation_A = (input_cursor >= relation_A_min_LBA && input_cursor <= relation_A_max_LBA);
	int estimated_tag_size = 1;// at_relation_A ? relation_A_size / num_partitions : relation_B_size / num_partitions;
	int lba = output_cursors[buffer_id]++;
	Event* write = new Event(WRITE, lba, estimated_tag_size, get_current_time());
	//if (use_tagging) write->set_tag(buffer_id * 2 + at_relation_A);
	//if (use_tagging) write->set_tag(1234);
	output_buffers[buffer_id] = 0;
	writes_in_progress++;
	//pending_ios.push(write);
	submit(write);
}


void Grace_Hash_Join::execute_probe_phase() {
	if (victim_buffer == num_partitions - 1 && large_bucket_cursor > large_bucket_end) {
		return;
	}
	// If we are done with a bucket pair, progress to the next, or, if we just started, begin with buffer zero
	bool first_run = (!finished_reading_smaller_bucket && small_bucket_end == 0 && large_bucket_cursor == 0 && large_bucket_end == 0);
	bool finished_with_current_bucket = (finished_reading_smaller_bucket && large_bucket_cursor > large_bucket_end);
	if (first_run || finished_with_current_bucket) {
		setup_probe_run();
	}

	if (finished_reading_smaller_bucket && large_bucket_cursor > large_bucket_end) {
		return;
	}

	//printf("Small %d:%d   Large %d:%d\n", small_bucket_cursor, small_bucket_end, large_bucket_cursor, large_bucket_end);

	if (reads_in_progress == 0 && !finished_reading_smaller_bucket) {
		//VisualTracer::get_instance()->print_horizontally(6000);
		read_smaller_bucket();
		phase = PROBE_ASYNCH;
	}
	else if (reads_in_progress == 0 && !finished_trimming_smaller_bucket) {
		//VisualTracer::get_instance()->print_horizontally(6000);
		trim_smaller_bucket();
		phase = PROBE_SYNCH;
	}
	else if (reads_in_progress == 0 && large_bucket_cursor <= large_bucket_end) {
		//VisualTracer::get_instance()->print_horizontally(6000);
		read_next_in_larger_bucket();
	} else if (reads_in_progress == 0) {
		assert(false);
	}
}

void Grace_Hash_Join::setup_probe_run() {
	if (reads_in_progress > 0) return; // Finish current reads before progressing
	assert(flex_reader == NULL || flex_reader->is_finished());
	victim_buffer++;
	//VisualTracer::get_instance()->print_horizontally(6000);
	// Set cursors corresponding to the chosen buffer
	if (PRINT_LEVEL >= 1) printf("Grace Hash Join, probe phase: Probing buffer %d/%d\n", victim_buffer, num_partitions-1);
	small_bucket_begin  = output_cursors_startpoints[victim_buffer];
	small_bucket_end    = output_cursors_splitpoints[victim_buffer];
	large_bucket_begin  = output_cursors_splitpoints[victim_buffer]+1;
	large_bucket_end    = output_cursors[victim_buffer]-1; // ?

	finished_reading_smaller_bucket = finished_trimming_smaller_bucket = false;
	large_bucket_cursor = large_bucket_begin;
	if (use_flexible_reads) {
		create_flexible_reader(small_bucket_begin, small_bucket_end);
	}
}

void Grace_Hash_Join::read_smaller_bucket() {
	for (int i = small_bucket_begin; i <= small_bucket_end; i++) {
		reads_in_progress_set.insert(i);
		reads_in_progress++;
		double time = get_current_time();
		Event* event = use_flexible_reads ? flex_reader->read_next(time) : new Event(READ, i, 1, time);
		submit(event);
	}
	finished_reading_smaller_bucket = true;
}

void Grace_Hash_Join::trim_smaller_bucket() {
	//printf("smaller buck:   %d\n", small_bucket_end - small_bucket_begin);
	for (int i = small_bucket_begin; i <= small_bucket_end; i++) {
		reads_in_progress_set.insert(i);
		Event* event = new Event(TRIM, i, 1, get_current_time());
		submit(event);
	}
	finished_trimming_smaller_bucket = true;
}

void Grace_Hash_Join::read_next_in_larger_bucket() {
	reads_in_progress_set.insert(large_bucket_cursor);
	reads_in_progress++;
	if (use_flexible_reads && large_bucket_cursor == large_bucket_begin) { // First time in this range
		create_flexible_reader(large_bucket_cursor, large_bucket_end);
	}
	double time = get_current_time();
	Event* read = use_flexible_reads ? flex_reader->read_next(time) : new Event(READ, large_bucket_cursor, 1, time);
	large_bucket_cursor++;
	submit(read);
}
