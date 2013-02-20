/*
 * scheduler.h
 *
 *  Created on: Feb 18, 2013
 *      Author: niv
 */

#ifndef SCHEDULER_H_
#define SCHEDULER_H_

#include "ssd.h"
#include "block_management.h"

namespace ssd {

class event_queue {
public:
	event_queue() : events(), num_events(0) {};
	virtual ~event_queue();
	virtual void push(Event*);
	vector<Event*> get_soonest_events();
	virtual bool remove(Event*);
	virtual Event* find(long dep_code) const;
	inline bool empty() const { return events.empty(); }
	double get_earliest_time() const { return floor((*events.begin()).first); };
	int size() const { return num_events; }
private:
	map<long, vector<Event*> > events;
	int num_events;
};

class Scheduling_Strategy : public event_queue {
public:
	Scheduling_Strategy(IOScheduler* s) : event_queue(), scheduler(s) {}
	virtual ~Scheduling_Strategy() {};
	virtual void register_event_completion(Event* e) {}
	virtual void schedule(int priorities_scheme = UNDEFINED) = 0;
protected:
	IOScheduler* scheduler;
};

inline bool overall_wait_time_comparator (const Event* i, const Event* j) {
	return i->get_overall_wait_time() < j->get_overall_wait_time();
}

inline bool current_wait_time_comparator (const Event* i, const Event* j) {
	return i->get_bus_wait_time() < j->get_bus_wait_time();
}

class Simple_Scheduling_Strategy : public Scheduling_Strategy {
public:
	Simple_Scheduling_Strategy(IOScheduler* s) : Scheduling_Strategy(s) {}
	~Simple_Scheduling_Strategy() {}
	void schedule(int priorities_scheme = UNDEFINED);
};

class Balancing_Scheduling_Strategy : public Simple_Scheduling_Strategy {
public:
	Balancing_Scheduling_Strategy(IOScheduler* s, Block_manager_parent* bm);
	~Balancing_Scheduling_Strategy();
	void schedule(int priorities_scheme = UNDEFINED);
	void push(Event*);
	void register_event_completion(Event*);
	bool remove(Event*);
	Event* find(long dep_code) const;
private:
	deque<Event*> internal_events;
	Block_manager_parent const*const bm;
	int num_writes_finished_since_last_internal_event;
	set<long> num_pending_application_writes;
};

class IOScheduler {
public:
	IOScheduler();
	~IOScheduler();
	void set_all(Ssd*, FtlParent*, Block_manager_parent*);
	void schedule_events_queue(deque<Event*> events);
	void schedule_event(Event* events);
	bool is_empty();
	void execute_soonest_events();
	void handle(vector<Event*>& events);
	void handle_noop_events(vector<Event*>& events);
private:
	void setup_structures(deque<Event*> events);
	enum status execute_next(Event* event);
	void execute_current_waiting_ios();
	void handle_event(Event* event);
	void handle_write(Event* event);
	void handle_flexible_read(Event* event);
	void transform_copyback(Event* event);
	void handle_finished_event(Event *event, enum status outcome);
	void remove_redundant_events(Event* new_event);
	bool should_event_be_scheduled(Event* event);
	void init_event(Event* event);
	void manage_operation_completion(Event* event);
	double get_soonest_event_time(vector<Event*> const& events) const;

	event_queue future_events;
	Scheduling_Strategy* current_events;

	map<uint, deque<Event*> > dependencies;

	Ssd* ssd;
	FtlParent* ftl;
	Block_manager_parent* bm;

	//map<uint, uint> LBA_to_dependencies;  // maps LBAs to dependency codes of GC operations. to be removed
	map<uint, uint> dependency_code_to_LBA;
	map<uint, event_type> dependency_code_to_type;
	map<uint, uint> LBA_currently_executing;

	map<uint, queue<uint> > op_code_to_dependent_op_codes;

	void update_current_events(double current_time);
	void remove_current_operation(Event* event);
	void promote_to_gc(Event* event_to_promote);
	void make_dependent(Event* dependent_event, uint independent_code);
	double get_current_time() const;
};

}


#endif /* SCHEDULER_H_ */
