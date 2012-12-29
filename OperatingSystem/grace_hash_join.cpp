/*
 * grace_hash_join.cpp
 *
 *  Created on: Nov 5, 2012
 *      Author: mks
 */

#include "../ssd.h"
#include "../MTRand/mtrand.h"

using namespace ssd;

int Grace_Hash_Join::grace_counter = 0;

Grace_Hash_Join::Grace_Hash_Join
       (long relation_A_min_LBA, long relation_A_max_LBA,
		long relation_B_min_LBA, long relation_B_max_LBA,
		long free_space_min_LBA, long free_space_max_LBA,
		long RAM_available,      double start_time,
		bool use_flexible_reads, bool use_tagging,
		long rows_per_page,      int randseed) :

		Thread(start_time),
		relation_A_min_LBA(relation_A_min_LBA), relation_A_max_LBA(relation_A_max_LBA),
		relation_B_min_LBA(relation_B_min_LBA), relation_B_max_LBA(relation_B_max_LBA),
		free_space_min_LBA(free_space_min_LBA), free_space_max_LBA(free_space_max_LBA),
		RAM_available(RAM_available),
        use_flexible_reads(use_flexible_reads), use_tagging(use_tagging),
		input_cursor(relation_A_min_LBA),
		rows_per_page(rows_per_page),
		phase(BUILD),
        flex_reader(NULL),
		random_number_generator(randseed),
		victim_buffer(UNDEFINED),
        small_bucket_begin(0), small_bucket_cursor(0), small_bucket_end(0),
        large_bucket_begin(0), large_bucket_cursor(0), large_bucket_end(0),
        trim_cursor(0),
        reads_in_progress(0),
        writes_in_progress(0)
{
	assert(relation_A_min_LBA < relation_A_max_LBA);
	assert(relation_B_min_LBA < relation_B_max_LBA);
	assert(free_space_min_LBA < free_space_max_LBA);

	relation_A_size = (relation_A_max_LBA - relation_A_min_LBA);
	relation_B_size = (relation_B_max_LBA - relation_B_min_LBA);
	free_space_size = (free_space_max_LBA - free_space_min_LBA);

	assert(RAM_available   <  relation_A_size + relation_B_size);
	assert(free_space_size >= relation_A_size + relation_B_size);

	long larger_relation_size = max(relation_A_size, relation_B_size);
	num_partitions = floor(sqrt(larger_relation_size));
	partition_size = free_space_size / num_partitions;
	buffer_size = (int) ((double) RAM_available / (num_partitions + 1) * rows_per_page);

	output_buffers.resize(num_partitions, 0);
	for (int i = 0; i < num_partitions; i++) {
		output_cursors.push_back(free_space_min_LBA + (int) (free_space_size * ((double) i / (num_partitions + 1))));
	}
	output_cursors_startpoints = vector<int>(output_cursors);
}

Event* Grace_Hash_Join::issue_next_io() {

	if (pending_writes.size() > 0) {
		Event* next_write = pending_writes.front();
		pending_writes.pop();
		writes_in_progress++;
		return next_write;
	}

	if (phase == BUILD) {
		return execute_build_phase();
	} else if (phase == PROBE_SYNC || phase == PROBE_ASYNC) {
		return execute_probe_phase();
	} else if (phase == DONE) {
		return NULL;
	}
	return NULL;
}

void Grace_Hash_Join::handle_read_completion_build(Event* event) {
	//put the records into an appropriate bucket
	for (int i = 0; i < rows_per_page; i++) {
		int bucket_index = random_number_generator() % num_partitions;
		int num_items = output_buffers[bucket_index]++;
		if (num_items == rows_per_page)
			flush_buffer(bucket_index);
	}

	// If we finished doing all the reads in the build phase, flush all the buckets
	bool done_reading = (input_cursor == relation_A_max_LBA + 1 || input_cursor == relation_B_max_LBA + 1);

	for (uint i = 0; i < output_buffers.size(); i++)
		if (done_reading && output_buffers[i] > 0)
			flush_buffer(i);

}

void Grace_Hash_Join::flush_buffer(int buffer_id) {
	bool at_relation_A = (input_cursor >= relation_A_min_LBA && input_cursor <= relation_A_max_LBA);
	int estimated_tag_size = at_relation_A ? relation_A_size / num_partitions : relation_B_size / num_partitions;
	int lba = output_cursors[buffer_id]++;
	Event* write = new Event(WRITE, lba, estimated_tag_size, time);
	if (use_tagging) write->set_tag(buffer_id * 2 + at_relation_A);
	pending_writes.push(write);
	output_buffers[buffer_id] = 0;

}

void Grace_Hash_Join::handle_event_completion(Event* event) {
	bool done_reading = (input_cursor == relation_A_max_LBA + 1 || input_cursor == relation_B_max_LBA + 1);

	// Maintain read/writes in progress bookkeeping variables
	if (event->get_event_type() == READ_TRANSFER) {
		reads_in_progress--;
		if (phase == BUILD) {
			handle_read_completion_build(event);
		}
		if (phase == PROBE_ASYNC && trim_cursor == small_bucket_begin && reads_in_progress == 0) { // The very last read we're waiting for (before we can trim)
		    time = max(time, event->get_current_time());
	    }
	} else if (event->get_event_type() == WRITE) {
		writes_in_progress--;
		if (done_reading && writes_in_progress == 0) { // The very last write we have been waiting for
			time = max(time, event->get_current_time());
		}
	}
	assert(event->get_event_type() != READ_COMMAND);
	assert(event->get_event_type() != READ);
	if (event->get_event_type() == READ_TRANSFER && (phase == BUILD || phase == PROBE_SYNC)) {
		time = max(time, event->get_current_time());
	}
}

Event* Grace_Hash_Join::execute_build_phase() {
	//for (uint i = 0; i < output_buffers.size(); i++) cout << " " << output_buffers[i] << "|"; cout << "\n"; // Printout of buffer utilization

	// The flexible reader initialization cannot be done in the constructor, since the OS object is not set at that point yet
	if (use_flexible_reads && flex_reader == NULL) {
		vector<Address_Range> ranges;
		ranges.push_back(Address_Range(relation_A_min_LBA, relation_A_max_LBA));
		assert(os != NULL);
		flex_reader = os->create_flexible_reader(ranges);
	}

	bool done_reading = (input_cursor == relation_A_max_LBA + 1 || input_cursor == relation_B_max_LBA + 1);

	if (done_reading) {
		if (writes_in_progress > 0) {
			//printf("Waiting for all writes to finish\n");
			return NULL; // Wait for the last writes to finish before continuing
		}

		// Since we are done reading the current relation, kill the associated flex reader
		if (use_flexible_reads) {
			assert(flex_reader->is_finished()); // Flex reader should agree with our internal bookkeeping that we're done
			delete flex_reader;
			flex_reader = NULL;
		}

		// If finished with reading relation 1, switch to relation 2
		if (input_cursor == relation_A_max_LBA + 1) {
			if (PRINT_LEVEL >= 1) printf("Grace Hash Join, build phase: Switching to relation 2\n");
			input_cursor = relation_B_min_LBA;
			output_cursors_splitpoints = vector<int>(output_cursors);
			if (use_flexible_reads) {
				vector<Address_Range> ranges;
				ranges.push_back(Address_Range(relation_B_min_LBA, relation_B_max_LBA));
				assert(os != NULL);
				flex_reader = os->create_flexible_reader(ranges);
			}

		// If done with reading both relations, switch to probe phase
		} else {
			phase = PROBE_SYNC;
			input_cursor = free_space_min_LBA;
			victim_buffer = UNDEFINED;
			return execute_probe_phase();
		}
	}

	// Only one read at the same time = synchronous reads
	if (reads_in_progress >= 1) return NULL;

	// Read new content to input buffer
	reads_in_progress++;
	if (use_flexible_reads) {
		input_cursor++;
		assert(!flex_reader->is_finished());
		return flex_reader->read_next(time);
	} else {
		return new Event(READ, input_cursor++, 1, time);
	}

}

void Grace_Hash_Join::create_flexible_reader(int start, int finish) {
	if (flex_reader != NULL) delete flex_reader;
	vector<Address_Range> ranges;
	//printf("(A) Range: %d -> %d\n", small_bucket_cursor, small_bucket_end);
	ranges.push_back(Address_Range(start, finish));
	assert(os != NULL);
	flex_reader = os->create_flexible_reader(ranges);
}

Event* Grace_Hash_Join::execute_probe_phase() {

	// If we have finished processing the last bucket, we are done with this phase
	if (victim_buffer == num_partitions - 1 && trim_cursor > large_bucket_end) {
		//printf("Very last event issued!\n");
		phase = DONE;
		finished = true;
		grace_counter++;
		printf("grace_counter: %d\n", grace_counter);
		if (71 == grace_counter) {
			PRINT_LEVEL = 1;
		}
		return NULL;
	}

	if (trim_cursor == small_bucket_end + 1) trim_cursor = large_bucket_begin;
	// If we are done with a bucket pair, progress to the next, or, if we just started, begin with buffer zero
	bool first_run = (small_bucket_cursor == 0 && small_bucket_end == 0 && large_bucket_cursor == 0 && large_bucket_end == 0);
	bool finished_with_current_bucket = (small_bucket_cursor > small_bucket_end && large_bucket_cursor > large_bucket_end && trim_cursor == large_bucket_end + 1);
	if (first_run || finished_with_current_bucket) {
		if (reads_in_progress >= 1) return NULL; // Finish current reads before progressing
		assert(flex_reader == NULL || flex_reader->is_finished());
		victim_buffer++;

		// Set cursors corresponding to the chosen buffer
		if (PRINT_LEVEL >= 1) printf("Grace Hash Join, probe phase: Probing buffer %d/%d\n", victim_buffer, num_partitions-1);

		small_bucket_begin  = output_cursors_startpoints[victim_buffer];
		small_bucket_end    = output_cursors_splitpoints[victim_buffer];
		large_bucket_begin  = output_cursors_splitpoints[victim_buffer]+1;
		large_bucket_end    = output_cursors[victim_buffer]-1; // ?
		if (small_bucket_end - small_bucket_cursor > large_bucket_end - large_bucket_cursor) {
			swap(small_bucket_begin, large_bucket_begin);
			swap(small_bucket_end,   large_bucket_end);
		}
		small_bucket_cursor = small_bucket_begin;
		large_bucket_cursor = large_bucket_begin;
		trim_cursor         = small_bucket_cursor;

		if (use_flexible_reads) {
			create_flexible_reader(small_bucket_cursor, small_bucket_end);
		}
	}

	if (small_bucket_cursor > small_bucket_end && large_bucket_cursor > large_bucket_end && trim_cursor <= large_bucket_end) {
		if (reads_in_progress >= 1) return NULL; // Finish reads into memory before trimming
		phase = PROBE_ASYNC;
		return new Event(TRIM, trim_cursor++, 1, time);
	}

	//printf("Small %d:%d   Large %d:%d\n", small_bucket_cursor, small_bucket_end, large_bucket_cursor, large_bucket_end);

	// If we are currently in the process of reading the small bucket into memory, continue with next page in buckets range
	if (small_bucket_cursor <= small_bucket_end) {
		reads_in_progress++;
		phase = PROBE_ASYNC;
		Event* event = use_flexible_reads ? flex_reader->read_next(time) : new Event(READ, small_bucket_cursor, 1, time);
		small_bucket_cursor++;
		return event;
	}
	// If we are in the process of reading the large bucket one page at a time (for joining with the small one in memory), continue with next page
	else {
		if (reads_in_progress >= 1) return NULL; // Only one read at the same time = synchronous reads
		reads_in_progress++;
		phase = PROBE_SYNC;
		if (use_flexible_reads && large_bucket_cursor == large_bucket_begin) { // First time in this range
			create_flexible_reader(large_bucket_cursor, large_bucket_end);
		}
		Event* event = use_flexible_reads ? flex_reader->read_next(time) : new Event(READ, large_bucket_cursor, 1, time);
		large_bucket_cursor++;
		return event;
	}

	assert(false); // A classical "this should never happen"
	return NULL;
}
