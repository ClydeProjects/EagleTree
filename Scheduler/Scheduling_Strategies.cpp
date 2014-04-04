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

int get_num_mimatches(vector<Event*>& events) {
	int mismatches = 0;
	set<double> values;
	if (events.size() == 0) {
		return 0;
	}
	values.insert(events[0]->get_bus_wait_time());
	for (int i = 1; i < events.size(); i++) {
		if (events[i-1]->get_bus_wait_time() > events[i]->get_bus_wait_time()) {
			mismatches++;
		}
		values.insert(events[i]->get_bus_wait_time());
	}
	printf("size:  %d    values:  %d    mismatches: %d\n", events.size(), values.size(), mismatches);
	if (mismatches > 10) {
		for (int i = 0; i < events.size(); i++) {
			printf("\t%f\t", events[i]->get_bus_wait_time());

			events[i]->print();
		}
	}


	return mismatches;
}

void Fifo_Priorty_Scheme::schedule(vector<Event*>& events) {
	event_queue q;
	for (auto e : events) {
		q.push(e, e->get_bus_wait_time());
	}
	while (q.size() > 0) {
		vector<Event*> ordered_events = q.get_soonest_events();
		scheduler->handle(ordered_events);
	}
}

void Noop_Priorty_Scheme::schedule(vector<Event*>& events) {
	scheduler->handle(events);
}

/*void Re_Er_Wr_Priorty_Scheme::schedule(vector<Event*>& reads, vector<Event*>& copyback_commands, vector<Event*>& writes, vector<Event*>& erases) {
	scheduler->handle(reads);
	scheduler->handle(copyback_commands);
	scheduler->handle(erases);
	scheduler->handle(writes);
}*/

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
