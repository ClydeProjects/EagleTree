#include "../ssd.h"
using namespace ssd;

void Simple_Scheduling_Strategy::schedule(int priorities_scheme) {
	priorities_scheme = priorities_scheme == UNDEFINED ? SCHEDULING_SCHEME : priorities_scheme;

	vector<Event*> current_events = get_soonest_events();
	vector<Event*> read_commands;
	vector<Event*> gc_read_commands;
	vector<Event*> read_commands_copybacks;
	vector<Event*> read_commands_flexible;
	vector<Event*> read_transfers;
	vector<Event*> gc_writes;
	vector<Event*> writes;
	vector<Event*> erases;
	vector<Event*> copy_backs;
	vector<Event*> noop_events;
	vector<Event*> trims;
	vector<Event*> overdue_events;

	while (current_events.size() > 0) {
		Event * event = current_events.back();
		current_events.pop_back();

		event_type type = event->get_event_type();
		bool is_GC = event->is_garbage_collection_op();

		if (event->is_cached_write()) {
			//printf("sending back cached:  %d\n", event->get_logical_address());
			//event->incr_bus_wait_time(50);
			ssd->register_event_completion(event);
		} else if (event->get_noop()) {
			noop_events.push_back(event);
		}
		else if (type == READ_COMMAND && event->is_copyback()) {
			read_commands_copybacks.push_back(event);
		}
		else if (type == READ_COMMAND && event->is_garbage_collection_op() && !event->is_original_application_io()) {
			gc_read_commands.push_back(event);
		}
		else if (type == READ_COMMAND && event->get_bus_wait_time() > READ_DEADLINE) {
			overdue_events.push_back(event);
		}
		else if (type == READ_COMMAND && event->is_flexible_read()) {
			read_commands_flexible.push_back(event);
		}
		else if (type == READ_COMMAND) {
			read_commands.push_back(event);
		}
		else if (type == READ_TRANSFER && event->get_bus_wait_time() >= READ_TRANSFER_DEADLINE) {
			overdue_events.push_back(event);
		}
		else if (type == READ_TRANSFER) {
			read_transfers.push_back(event);
		}
		else if (type == WRITE) {
			if (event->get_bus_wait_time() > WRITE_DEADLINE /*&& event->is_original_application_io()*/) {
				overdue_events.push_back(event);
			} else if (is_GC && !event->is_original_application_io()) {
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

	/*uint num_writes = writes.size() + overdue_writes.size();
	uint num_reads = read_commands.size();
	printf("%d\t%d\n", num_writes, num_reads);*/

	scheduler->handle(trims);

	//handle_writes(copy_backs); // Copy backs should be prioritized first to avoid conflict, since they have a reserved page waiting to be written
	//read_commands.insert(read_commands.end(), gc_read_commands.begin(), gc_read_commands.end());
	// Intuitive scheme. Prioritize Application IOs
	if (priorities_scheme == 0) {
		read_commands.insert(read_commands.end(), read_commands_flexible.begin(), read_commands_flexible.end());
		read_commands.insert(read_commands.end(), gc_read_commands.begin(), gc_read_commands.end());

		writes.insert(writes.end(), gc_writes.begin(), gc_writes.end());
		read_transfers.insert(read_transfers.end(), copy_backs.begin(), copy_backs.end());

		sort(read_commands.begin(), read_commands.end(), overall_wait_time_comparator);
		sort(writes.begin(), writes.end(), current_wait_time_comparator);
		sort(overdue_events.begin(), overdue_events.end(), current_wait_time_comparator);
		sort(read_commands_copybacks.begin(), read_commands_copybacks.end(), overall_wait_time_comparator);

		scheduler->handle(overdue_events);
		scheduler->handle(read_commands);
		scheduler->handle(read_commands_copybacks);
		scheduler->handle(erases);
		scheduler->handle(writes);
	}
	// Traditional - GC PRIORITY
	else if (priorities_scheme == 1) {
		read_commands.insert(read_commands.end(), read_commands_flexible.begin(), read_commands_flexible.end());
		//writes.insert(writes.end(), gc_writes.begin(), gc_writes.end());

		scheduler->handle(erases);
		scheduler->handle(gc_writes);
		scheduler->handle(read_commands);
		scheduler->handle(writes);
	}
	else if (priorities_scheme == 2) {
		read_commands.insert(read_commands.end(), read_commands_flexible.begin(), read_commands_flexible.end());

		writes.insert(writes.end(), gc_writes.begin(), gc_writes.end());
		//read_commands.insert(read_commands.end(), gc_read_commands.begin(), gc_read_commands.end());
		read_transfers.insert(read_transfers.end(), copy_backs.begin(), copy_backs.end());

		sort(read_commands.begin(), read_commands.end(), overall_wait_time_comparator);
		sort(gc_read_commands.begin(), gc_read_commands.end(), overall_wait_time_comparator);
		sort(writes.begin(), writes.end(), current_wait_time_comparator);
		sort(overdue_events.begin(), overdue_events.end(), current_wait_time_comparator);
		sort(read_commands_copybacks.begin(), read_commands_copybacks.end(), overall_wait_time_comparator);


		scheduler->handle(overdue_events);
		scheduler->handle(read_commands);
		scheduler->handle(gc_read_commands);
		scheduler->handle(read_commands_copybacks);
		//scheduler->handle(gc_writes);
		scheduler->handle(erases);
		scheduler->handle(writes);

	}
	// FLEXIBLE READS AND WRITES EQUAL PRIORITY
	else if (priorities_scheme == 3) {
		writes.insert(writes.end(), gc_writes.begin(), gc_writes.end());

		// Put flexible reads in write vector - simple but ugly way to give flexible reads and writes equal priority
		writes.insert(writes.end(), read_commands_flexible.begin(), read_commands_flexible.end());
		read_transfers.insert(read_transfers.end(), copy_backs.begin(), copy_backs.end());
		//read_commands.insert(read_commands.end(), read_commands_copybacks.begin(), read_commands_copybacks.end());

		//sort(erases.begin(), erases.end(), current_wait_time_comparator);
		sort(read_commands.begin(), read_commands.end(), overall_wait_time_comparator);
		sort(writes.begin(), writes.end(), current_wait_time_comparator);
		//sort(read_transfers.begin(), read_transfers.end(), overall_wait_time_comparator);
		sort(read_commands_copybacks.begin(), read_commands_copybacks.end(), overall_wait_time_comparator);

		scheduler->handle(read_commands);
		scheduler->handle(gc_read_commands);
		scheduler->handle(read_commands_copybacks);
		//handle(read_commands_copybacks);
		scheduler->handle(writes);
		scheduler->handle(erases);
		//handle(copy_backs);
	} else if (priorities_scheme == 4) {
		// PURE FIFO STRATEGY
		vector<Event*> all_ios;
		all_ios.insert(all_ios.end(), read_commands_flexible.begin(), read_commands_flexible.end());
		all_ios.insert(all_ios.end(), gc_read_commands.begin(), gc_read_commands.end());
		all_ios.insert(all_ios.end(), read_commands.begin(), read_commands.end());
		all_ios.insert(all_ios.end(), gc_writes.begin(), gc_writes.end());
		all_ios.insert(all_ios.end(), writes.begin(), writes.end());
		all_ios.insert(all_ios.end(), copy_backs.begin(), copy_backs.end());
		all_ios.insert(all_ios.end(), erases.begin(), erases.end());
		sort(all_ios.begin(), all_ios.end(), current_wait_time_comparator);
		scheduler->handle(all_ios);
	} else if (priorities_scheme == 5) {
		read_commands.insert(read_commands.end(), read_commands_flexible.begin(), read_commands_flexible.end());
		//writes.insert(writes.end(), gc_writes.begin(), gc_writes.end());
		read_transfers.insert(read_transfers.end(), copy_backs.begin(), copy_backs.end());
		//read_commands.insert(read_commands.end(), read_commands_copybacks.begin(), read_commands_copybacks.end());

		//sort(erases.begin(), erases.end(), current_wait_time_comparator);
		sort(read_commands.begin(), read_commands.end(), overall_wait_time_comparator);
		sort(gc_read_commands.begin(), gc_read_commands.end(), overall_wait_time_comparator);
		sort(writes.begin(), writes.end(), current_wait_time_comparator);
		//sort(read_transfers.begin(), read_transfers.end(), overall_wait_time_comparator);
		sort(read_commands_copybacks.begin(), read_commands_copybacks.end(), overall_wait_time_comparator);

		scheduler->handle(gc_read_commands);
		scheduler->handle(gc_writes);
		scheduler->handle(read_commands);

		scheduler->handle(read_commands_copybacks);
		scheduler->handle(erases);
		//handle(read_commands_copybacks);

		scheduler->handle(writes);
		//handle(copy_backs);
	} else {
		assert(false);
	}

	scheduler->handle(read_transfers);
	scheduler->handle_noop_events(noop_events);
}

/**********************************************************************
 *
 */

Balancing_Scheduling_Strategy::Balancing_Scheduling_Strategy(IOScheduler* s, Block_manager_parent* bm, Ssd* ssd)
  : Simple_Scheduling_Strategy(s, ssd),
    bm(bm),
    num_writes_finished_since_last_internal_event(0),
    num_pending_application_writes()
{}

Balancing_Scheduling_Strategy::~Balancing_Scheduling_Strategy() {
	while (!internal_events.empty()) {
		Event* e = internal_events.front();
		internal_events.pop_front();
		delete e;
	}
}

void Balancing_Scheduling_Strategy::schedule(int priorities_scheme) {
	Simple_Scheduling_Strategy::schedule(5);
	int i = internal_events.size();
	i++;
}

void Balancing_Scheduling_Strategy::push(Event* event) {
	if (event->get_event_type() == WRITE && event->is_original_application_io()) {
		num_pending_application_writes.insert(event->get_application_io_id());
	}
	if (!event->is_original_application_io() && event->get_event_type() == READ_COMMAND && event->get_bus_wait_time() == 0 && (event->is_garbage_collection_op() || event->is_wear_leveling_op() )) {
		internal_events.push_back(event);
	} else {
		event_queue::push(event);
	}
}

void Balancing_Scheduling_Strategy::register_event_completion(Event* e) {
	if (/*e->get_event_type() == WRITE &&*/ e->is_original_application_io()) {
		num_writes_finished_since_last_internal_event++;
		num_pending_application_writes.erase(e->get_application_io_id());
	}
	/*if (num_pending_application_writes.size() == 0 && e->is_original_application_io()) {
		num_writes_finished_since_last_internal_event++;
	}*/
	double num_writes_per_gc = BLOCK_SIZE / bm->get_average_migrations_per_gc() - 1;
	//printf("num_writes_per_gc  %f\n", num_writes_per_gc);
	if (num_writes_finished_since_last_internal_event >= num_writes_per_gc && !internal_events.empty()) {
		num_writes_finished_since_last_internal_event = 0;
		Event* e = internal_events.front();
		internal_events.pop_front();
		event_queue::push(e);
	}
	//assert(num_pending_application_writes.size() > 0);
}

bool Balancing_Scheduling_Strategy::remove(Event* event) {
	bool success = event_queue::remove(event);
	if (!success) {
		deque<Event*>::iterator iter = std::find(internal_events.begin(), internal_events.end(), event);
		if (iter != internal_events.end()) {
			delete (*iter);
			internal_events.erase(iter);
			success = true;
		}
	}
	if (success && event->get_event_type() == WRITE && event->is_original_application_io()) {
		num_pending_application_writes.erase(event->get_application_io_id());
	}
	return success;
}

Event* Balancing_Scheduling_Strategy::find(long dep_code) const {
	Event* event = event_queue::find(dep_code);
	int s =  internal_events.size();
	for (uint i = 0; i < internal_events.size() && event == NULL; i++) {
		assert(internal_events[i] != NULL);
		if (internal_events[i]->get_application_io_id() == dep_code) {
			return internal_events[i];
		}
	}
	return event;
}
