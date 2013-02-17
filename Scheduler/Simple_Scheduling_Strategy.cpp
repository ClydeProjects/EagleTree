#include "../ssd.h"
using namespace ssd;

void Simple_Scheduling_Strategy::schedule() {
	vector<Event*> current_events = get_soonest_events();
	vector<Event*> read_commands;
	vector<Event*> read_commands_gc;
	vector<Event*> read_commands_copybacks;
	vector<Event*> read_commands_flexible;
	//vector<Event*> gc_read_commands;
	vector<Event*> read_transfers;
	vector<Event*> gc_writes;
	vector<Event*> writes;
	vector<Event*> erases;
	vector<Event*> copy_backs;
	vector<Event*> noop_events;
	vector<Event*> trims;

	while (current_events.size() > 0) {
		Event * event = current_events.back();
		current_events.pop_back();

		event_type type = event->get_event_type();
		bool is_GC = event->is_garbage_collection_op();

		if (event->get_noop()) {
			noop_events.push_back(event);
		}
		else if (type == READ_COMMAND && event->is_copyback()) {
			read_commands_copybacks.push_back(event);
		}
		else if (type == READ_COMMAND && event->is_garbage_collection_op() && !event->is_original_application_io()) {
			read_commands_gc.push_back(event);
		}
		else if (type == READ_COMMAND && event->is_flexible_read()) {
			read_commands_flexible.push_back(event);
		}
		else if (type == READ_COMMAND) {
			read_commands.push_back(event);
		}
		else if (type == READ_TRANSFER) {
			read_transfers.push_back(event);
		}
		else if (type == WRITE) {
			if (is_GC) {
				gc_writes.push_back(event);
			} else {
				writes.push_back(event);
			}
		}
		else if (type == ERASE) {
			erases.push_back(event);
		}
		else if (type == COPY_BACK) {
			copy_backs.push_back(event);
		}
		else if (type == TRIM) {
			trims.push_back(event);
		}
	}

	scheduler->handle(trims);

	//handle_writes(copy_backs); // Copy backs should be prioritized first to avoid conflict, since they have a reserved page waiting to be written
	//read_commands.insert(read_commands.end(), gc_read_commands.begin(), gc_read_commands.end());
	// Intuitive scheme. Prioritize Application IOs
	if (SCHEDULING_SCHEME == 0) {
		read_commands.insert(read_commands.end(), read_commands_flexible.begin(), read_commands_flexible.end());

		sort(erases.begin(), erases.end(), current_wait_time_comparator);
		sort(read_commands.begin(), read_commands.end(), current_wait_time_comparator);
		sort(writes.begin(), writes.end(), current_wait_time_comparator);
		sort(gc_writes.begin(), gc_writes.end(), overall_wait_time_comparator);
		sort(read_transfers.begin(), read_transfers.end(), overall_wait_time_comparator);

		scheduler->handle(read_commands);
		scheduler->handle(read_transfers);
		scheduler->handle(writes);
		scheduler->handle(gc_writes);
		scheduler->handle(erases);
	}
	// Traditional - GC PRIORITY
	else if (SCHEDULING_SCHEME == 1) {
		read_commands.insert(read_commands.end(), read_commands_flexible.begin(), read_commands_flexible.end());
		//writes.insert(writes.end(), gc_writes.begin(), gc_writes.end());

		scheduler->handle(erases);
		scheduler->handle(gc_writes);
		scheduler->handle(read_commands);
		scheduler->handle(writes);
		scheduler->handle(read_transfers);
	}
	else if (SCHEDULING_SCHEME == 2) {
		read_commands.insert(read_commands.end(), read_commands_flexible.begin(), read_commands_flexible.end());

		writes.insert(writes.end(), gc_writes.begin(), gc_writes.end());
		read_transfers.insert(read_transfers.end(), copy_backs.begin(), copy_backs.end());
		//read_commands.insert(read_commands.end(), read_commands_copybacks.begin(), read_commands_copybacks.end());

		sort(erases.begin(), erases.end(), current_wait_time_comparator);
		sort(read_commands.begin(), read_commands.end(), overall_wait_time_comparator);
		sort(read_commands_gc.begin(), read_commands_gc.end(), overall_wait_time_comparator);
		sort(writes.begin(), writes.end(), current_wait_time_comparator);
		sort(read_transfers.begin(), read_transfers.end(), overall_wait_time_comparator);
		sort(read_commands_copybacks.begin(), read_commands_copybacks.end(), overall_wait_time_comparator);


		scheduler->handle(read_commands);
		scheduler->handle(read_commands_gc);
		scheduler->handle(read_commands_copybacks);
		scheduler->handle(erases);
		//handle(read_commands_copybacks);
		scheduler->handle(writes);
		scheduler->handle(read_transfers);
		//handle(copy_backs);
	}

	// FLEXIBLE READS AND WRITES EQUAL PRIORITY
	else if (SCHEDULING_SCHEME == 3) {
		writes.insert(writes.end(), gc_writes.begin(), gc_writes.end());

		// Put flexible reads in write vector - simple but ugly way to give flexible reads and writes equal priority
		writes.insert(writes.end(), read_commands_flexible.begin(), read_commands_flexible.end());
		read_transfers.insert(read_transfers.end(), copy_backs.begin(), copy_backs.end());
		//read_commands.insert(read_commands.end(), read_commands_copybacks.begin(), read_commands_copybacks.end());

		sort(erases.begin(), erases.end(), current_wait_time_comparator);
		sort(read_commands.begin(), read_commands.end(), overall_wait_time_comparator);
		sort(writes.begin(), writes.end(), current_wait_time_comparator);
		sort(read_transfers.begin(), read_transfers.end(), overall_wait_time_comparator);
		sort(read_commands_copybacks.begin(), read_commands_copybacks.end(), overall_wait_time_comparator);

		scheduler->handle(erases);
		scheduler->handle(read_commands);
		scheduler->handle(read_commands_copybacks);
		//handle(read_commands_copybacks);
		scheduler->handle(writes);
		scheduler->handle(read_transfers);
		//handle(copy_backs);
	} else if (SCHEDULING_SCHEME == 4) {
		// PURE FIFO STRATEGY
		vector<Event*> all_ios;
		all_ios.insert(all_ios.end(), read_commands_flexible.begin(), read_commands_flexible.end());
		all_ios.insert(all_ios.end(), read_commands_gc.begin(), read_commands_gc.end());
		all_ios.insert(all_ios.end(), read_commands.begin(), read_commands.end());
		all_ios.insert(all_ios.end(), gc_writes.begin(), gc_writes.end());
		all_ios.insert(all_ios.end(), writes.begin(), writes.end());
		all_ios.insert(all_ios.end(), read_transfers.begin(), read_transfers.end());
		all_ios.insert(all_ios.end(), copy_backs.begin(), copy_backs.end());
		all_ios.insert(all_ios.end(), erases.begin(), erases.end());
		sort(all_ios.begin(), all_ios.end(), current_wait_time_comparator);
		scheduler->handle(all_ios);
	} else {
		assert(false);
	}

	scheduler->handle_noop_events(noop_events);
}
