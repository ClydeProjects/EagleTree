#include "../ssd.h"
using namespace ssd;

void Priorty_Scheme::seperate_internal_external(vector<Event*> const& events, vector<Event*>& internal, vector<Event*>& external) {
	for (uint i = 0; i < events.size(); i++) {
		if (events[i]->is_original_application_io()) {
			external.push_back(events[i]);
		} else {
			internal.push_back(events[i]);
		}
	}
}

void Priorty_Scheme::seperate_by_type(vector<Event*> const& events, vector<Event*>& read_commands, vector<Event*>& copyback_commands, vector<Event*>& writes, vector<Event*>& erases) {
	for (auto e : events) {
		switch (e->get_event_type()) {
			case READ_COMMAND: 	read_commands.push_back(e); 		break;
			case WRITE: 		writes.push_back(e);				break;
			case COPY_BACK:		copyback_commands.push_back(e); 	break;
			case ERASE:			erases.push_back(e);				break;
		}
	}
}

void Fifo_Priorty_Scheme::schedule(vector<Event*>& events) {
	event_queue q;
	for (auto e : events) {
		q.push(e, INFINITE - e->get_bus_wait_time());
	}
	while (q.size() > 0) {
		vector<Event*> ordered_events = q.get_soonest_events();
		scheduler->handle(ordered_events);
	}
}

void Semi_Fifo_Priorty_Scheme::schedule(vector<Event*>& events) {
	long max = -1;
	int chosen_index = 0;
	for (int i = 0; i < events.size(); i++) {
		if (events[i] != NULL && events[i]->get_bus_wait_time() > max) {
			max = events[i]->get_bus_wait_time();
			chosen_index = i;
		}
	}
	if (events.size() > 0) {
		Event* chosen = events[chosen_index];
		events[chosen_index] = NULL;
		scheduler->handle(chosen);
		//vector<Event*> chosen_vec(1, chosen);
		//scheduler->handle(chosen_vec);
	}
	scheduler->handle(events);
}

void Noop_Priorty_Scheme::schedule(vector<Event*>& events) {
	scheduler->handle(events);
}

void Re_Er_Wr_Priorty_Scheme::schedule(vector<Event*>& events) {
	vector<Event*> reads, copybacks, writes, erases;
	seperate_by_type(events, reads, copybacks, writes, erases);
	scheduler->handle(reads);
	sort(reads.begin(), reads.end(), current_wait_time_comparator);
	scheduler->handle(copybacks);
	scheduler->handle(erases);
	sort(writes.begin(), writes.end(), current_wait_time_comparator);
	//return_to_queue_excessive_events(writes);
	//prioritize_oldest_event(writes);
	scheduler->handle(writes);
}

void Smart_App_Priorty_Scheme::schedule(vector<Event*>& events) {
	vector<Event*> reads, copybacks, writes, erases;
	seperate_by_type(events, reads, copybacks, writes, erases);

	vector<Event*> internal_reads;
	vector<Event*> external_reads;
	seperate_internal_external(reads, internal_reads, external_reads);

	vector<Event*> internal_writes;
	vector<Event*> external_writes;
	seperate_internal_external(writes, internal_writes, external_writes);

	scheduler->handle(external_reads);
	scheduler->handle(internal_reads);
	scheduler->handle(copybacks);
	scheduler->handle(erases);
	scheduler->handle(external_writes);
	scheduler->handle(internal_writes);
}


void Er_Wr_Re_gcRe_gcWr_Priorty_Scheme::schedule(vector<Event*>& events) {
	vector<Event*> reads, copybacks, writes, erases;
	seperate_by_type(events, reads, copybacks, writes, erases);

	vector<Event*> internal_reads;
	vector<Event*> external_reads;
	seperate_internal_external(reads, internal_reads, external_reads);

	vector<Event*> internal_writes;
	vector<Event*> external_writes;
	seperate_internal_external(writes, internal_writes, external_writes);

	scheduler->handle(erases);
	scheduler->handle(external_writes);
	scheduler->handle(external_reads);
	scheduler->handle(internal_reads);
	scheduler->handle(internal_writes);
	//scheduler->handle(copyback_commands);

}

void gcRe_gcWr_Er_Re_Wr_Priorty_Scheme::schedule(vector<Event*>& events) {
	vector<Event*> reads, copybacks, writes, erases;
	seperate_by_type(events, reads, copybacks, writes, erases);

	vector<Event*> internal_reads;
	vector<Event*> external_reads;
	seperate_internal_external(reads, internal_reads, external_reads);

	vector<Event*> internal_writes;
	vector<Event*> external_writes;
	seperate_internal_external(writes, internal_writes, external_writes);

	scheduler->handle(erases);
	scheduler->handle(internal_reads);
	scheduler->handle(internal_writes);

	scheduler->handle(external_reads);
	scheduler->handle(copybacks);
	scheduler->handle(external_writes);
}

void We_Re_gcWr_E_gcR_Priorty_Scheme::schedule(vector<Event*>& events) {
	vector<Event*> reads, copybacks, writes, erases;
	seperate_by_type(events, reads, copybacks, writes, erases);

	vector<Event*> internal_reads;
	vector<Event*> external_reads;
	seperate_internal_external(reads, internal_reads, external_reads);

	vector<Event*> internal_writes;
	vector<Event*> external_writes;
	seperate_internal_external(writes, internal_writes, external_writes);

	scheduler->handle(external_writes);
	scheduler->handle(internal_reads);
	scheduler->handle(internal_writes);
	scheduler->handle(erases);
	scheduler->handle(external_reads);
	scheduler->handle(copybacks);

}

void Scheduling_Strategy::schedule() {
	//print();
	vector<Event*> current_events = get_soonest_events();

	/*cout << current_events.size() << endl;
	float gc = 0;
	for (auto e : current_events) {
		if (e->is_garbage_collection_op()) {
			gc++;
		}
	}
	printf("%d\t%f\t%f", current_events.size(), gc, gc / (float)current_events.size());
*/

	vector<Event*> read_transfers;
	vector<Event*> noop_events;
	vector<Event*> trims;
	vector<Event*> others;
	others.reserve(current_events.size());

	for (uint i = 0; i < current_events.size(); i++) {
		Event * event = current_events[i];
		event_type type = event->get_event_type();

		if (event->is_cached_write()) {
			ssd->register_event_completion(event);
		} else if (event->get_noop()) {
			noop_events.push_back(event);
		}
		else {
			switch (type) {
			  case TRIM 			: trims.push_back(event);			break;
			  case READ_TRANSFER 	: read_transfers.push_back(event);	break;
			  default				: others.push_back(event); 			break;
			}
		}
	}

	//sort(reads.begin(), reads.end(), current_wait_time_comparator);
	//sort(copybacks.begin(), copybacks.end(), current_wait_time_comparator);
	//sort(writes.begin(), writes.end(), current_wait_time_comparator);

	/*uint num_writes = writes.size() + overdue_writes.size();
	uint num_reads = read_commands.size();
	printf("%d\t%d\n", num_writes, num_reads);*/

	//printf("size: %d\n", writes.size());
	scheduler->handle(trims);
	priorty_scheme->schedule(others);
	scheduler->handle(read_transfers);
	scheduler->handle_noop_events(noop_events);
}
