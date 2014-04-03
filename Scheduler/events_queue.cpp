
#include "../ssd.h"
using namespace ssd;

vector<Event*> event_queue::get_soonest_events() {
	vector<Event*> soonest_events = (*events.begin()).second;
	//printf("num_events:  %d\n", soonest_events.size());
	num_events -= soonest_events.size();
	events.erase(events.begin());
	return soonest_events;
}

void event_queue::push(Event* event, double value) {
	num_events++;
	//printf("num_events:  %d\n", num_events);
	if (events.count(value) == 0) {
		vector<Event*> new_events(1, event);
		new_events.reserve(10);
		events[value] = new_events;
	} else {
		vector<Event*>& events_with_this_time = events[value];
		events_with_this_time.push_back(event);
	}
}

void event_queue::push(Event* event) {
	num_events++;
	//printf("num_events:  %d\n", num_events);
	long current_time = floor(event->get_current_time());
	if (events.count(current_time) == 0) {
		vector<Event*> new_events(1, event);
		new_events.reserve(50);
		events[current_time] = new_events;
	} else {
		vector<Event*>& events_with_this_time = events[current_time];
		events_with_this_time.push_back(event);
	}
}

Event* event_queue::find(long dependency_code) const {
	map<long, vector<Event*> >::const_iterator k = events.begin();
	for (; k != events.end(); k++) {
		vector<Event*> events = (*k).second;
		for (uint j = 0; j < events.size(); j++) {
			if (events[j]->get_application_io_id() == dependency_code) {
				return events[j];
			}
		}
	}
	return NULL;
}

bool event_queue::remove(Event* event) {
	num_events--;
	if (event == NULL) return false;
	long time = event->get_current_time();
	vector<Event*>& events_with_time = events[time];
	vector<Event*>::iterator iter = std::find(events_with_time.begin(), events_with_time.end(), event);
	if (iter == events_with_time.end())
		return false;
	events_with_time.erase(iter);
	if (events_with_time.size() == 0) {
		events.erase(time);
	}
	return true;
}

event_queue::~event_queue() {
	map<long, vector<Event*> >::iterator k = events.begin();
	for (; k != events.end(); k++) {
		vector<Event*>& events = (*k).second;
		for (uint j = 0; j < events.size(); j++) {
			events[j]->print();
			delete events[j];
		}
	}
}
