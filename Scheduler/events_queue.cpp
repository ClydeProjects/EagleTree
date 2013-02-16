
#include "../ssd.h"

using namespace ssd;

vector<Event*> IOScheduler::event_queue::get_soonest_events() {
	vector<Event*> soonest_events = (*events.begin()).second;
	num_events =- soonest_events.size();
	events.erase(events.begin());
	return soonest_events;
}

void IOScheduler::event_queue::push(Event* event) {
	num_events++;
	long current_time = floor(event->get_current_time());
	if (events.count(current_time) == 0) {
		vector<Event*> new_events;
		new_events.push_back(event);
		events[current_time] = new_events;
	} else {
		vector<Event*>& events_with_this_time = events[current_time];
		events_with_this_time.push_back(event);
	}
}

Event* IOScheduler::event_queue::find(long dependency_code) {
	map<long, vector<Event*> >::iterator k = events.begin();
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

bool IOScheduler::event_queue::remove(Event* event) {
	long time = event->get_current_time();
	vector<Event*>& events_with_time = events[time];
	vector<Event*>::iterator e = std::find(events_with_time.begin(), events_with_time.end(), event);
	if (e == events_with_time.end())
		return false;
	events_with_time.erase(e);
	return true;
}

IOScheduler::event_queue::~event_queue() {
	map<long, vector<Event*> >::iterator k = events.begin();
	for (; k != events.end(); k++) {
		vector<Event*>& events = (*k).second;
		for (uint j = 0; j < events.size(); j++) {
			delete events[j];
		}
	}
}
