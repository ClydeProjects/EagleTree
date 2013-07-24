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

void Fifo_Priorty_Scheme::schedule(vector<Event*>& reads, vector<Event*>& copyback_commands, vector<Event*>& writes, vector<Event*>& erases) {
	vector<Event*> all_ios;

	all_ios.insert(all_ios.end(), reads.begin(), reads.end());
	all_ios.insert(all_ios.end(), writes.begin(), writes.end());
	all_ios.insert(all_ios.end(), erases.begin(), erases.end());
	all_ios.insert(all_ios.end(), copyback_commands.begin(), copyback_commands.end());

	sort(all_ios.begin(), all_ios.end(), current_wait_time_comparator);
	scheduler->handle(all_ios);
}

void Re_Er_Wr_Priorty_Scheme::schedule(vector<Event*>& reads, vector<Event*>& copyback_commands, vector<Event*>& writes, vector<Event*>& erases) {
	scheduler->handle(reads);
	scheduler->handle(copyback_commands);
	scheduler->handle(erases);
	scheduler->handle(writes);
}

void Er_gcRe_gcWr_Re_Wr_Priorty_Scheme::schedule(vector<Event*>& reads, vector<Event*>& copyback_commands, vector<Event*>& writes, vector<Event*>& erases) {
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
	scheduler->handle(copyback_commands);
	scheduler->handle(external_writes);
}


void Smart_App_Priorty_Scheme::schedule(vector<Event*>& reads, vector<Event*>& copybacks, vector<Event*>& writes, vector<Event*>& erases) {
	vector<Event*> internal_reads;
	vector<Event*> external_reads;
	seperate_internal_external(reads, internal_reads, external_reads);

	vector<Event*> internal_writes;
	vector<Event*> external_writes;
	seperate_internal_external(writes, internal_writes, external_writes);

	scheduler->handle(internal_reads);
	scheduler->handle(external_reads);
	scheduler->handle(copybacks);
	scheduler->handle(erases);
	scheduler->handle(external_writes);
	scheduler->handle(internal_writes);
}

void Scheduling_Strategy::schedule() {

	vector<Event*> current_events = get_soonest_events();
	vector<Event*> reads;
	vector<Event*> copybacks;
	vector<Event*> read_transfers;
	vector<Event*> writes;
	vector<Event*> erases;
	vector<Event*> noop_events;
	vector<Event*> trims;

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
			  case READ_COMMAND 	: reads.push_back(event); 			break;
			  case READ_TRANSFER 	: read_transfers.push_back(event);	break;
			  case WRITE 			: writes.push_back(event);			break;
			  case ERASE 			: erases.push_back(event);			break;
			  case COPY_BACK 		: copybacks.push_back(event);		break;
			  case TRIM 			: trims.push_back(event);			break;
			}
		}
	}

	sort(reads.begin(), reads.end(), current_wait_time_comparator);
	sort(copybacks.begin(), copybacks.end(), current_wait_time_comparator);
	sort(writes.begin(), writes.end(), current_wait_time_comparator);

	/*uint num_writes = writes.size() + overdue_writes.size();
	uint num_reads = read_commands.size();
	printf("%d\t%d\n", num_writes, num_reads);*/

	//printf("size: %d\n", writes.size());
	scheduler->handle(trims);
	priorty_scheme->schedule(reads, copybacks, writes, erases);
	scheduler->handle(read_transfers);
	scheduler->handle_noop_events(noop_events);
}
