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

class Priorty_Scheme {
public:
	Priorty_Scheme(IOScheduler* scheduler) : scheduler(scheduler) {}
	virtual ~Priorty_Scheme() {};
	virtual void schedule(vector<Event*>& read_commands, vector<Event*>& copyback_commands, vector<Event*>& writes, vector<Event*>& erases) = 0;
	void seperate_internal_external(vector<Event*> const& events, vector<Event*>& internal, vector<Event*>& external);
protected:
	IOScheduler* scheduler;
};

class Fifo_Priorty_Scheme : public Priorty_Scheme {
public:
	Fifo_Priorty_Scheme(IOScheduler* scheduler) : Priorty_Scheme(scheduler) {};
	void schedule(vector<Event*>& read_commands, vector<Event*>& copyback_commands, vector<Event*>& writes, vector<Event*>& erases);
};

class Re_Er_Wr_Priorty_Scheme : public Priorty_Scheme {
public:
	Re_Er_Wr_Priorty_Scheme(IOScheduler* scheduler) : Priorty_Scheme(scheduler) {};
	void schedule(vector<Event*>& read_commands, vector<Event*>& copyback_commands, vector<Event*>& writes, vector<Event*>& erases);
};

class Er_gcRe_gcWr_Re_Wr_Priorty_Scheme : public Priorty_Scheme {
public:
	Er_gcRe_gcWr_Re_Wr_Priorty_Scheme(IOScheduler* scheduler) : Priorty_Scheme(scheduler) {};
	void schedule(vector<Event*>& read_commands, vector<Event*>& copyback_commands, vector<Event*>& writes, vector<Event*>& erases);
};

class Smart_App_Priorty_Scheme : public Priorty_Scheme {
public:
	Smart_App_Priorty_Scheme(IOScheduler* scheduler) : Priorty_Scheme(scheduler) {};
	void schedule(vector<Event*>& read_commands, vector<Event*>& copyback_commands, vector<Event*>& writes, vector<Event*>& erases);
};

class event_queue {
public:
	event_queue() : events(), num_events(0) {};
	virtual ~event_queue();
	virtual void push(Event*);
	vector<Event*> get_soonest_events();
	virtual bool remove(Event*);
	virtual Event* find(long dep_code) const;
	inline bool empty() const { return events.empty(); }
	double get_earliest_time() const { return events.empty() ? 0 : floor((*events.begin()).first); };
	int size() const { return num_events; }
private:
	map<long, vector<Event*> > events;
	int num_events;
};

class Scheduling_Strategy : public event_queue {
public:
	Scheduling_Strategy(IOScheduler* s, Ssd* ssd, Priorty_Scheme* scheme) : event_queue(), scheduler(s), ssd(ssd), priorty_scheme(scheme) {}
	virtual ~Scheduling_Strategy() {};
	virtual void schedule();
protected:
	IOScheduler* scheduler;
	Ssd* ssd;
	Priorty_Scheme* priorty_scheme;
};

inline bool overall_wait_time_comparator (const Event* i, const Event* j) {
	return i->get_overall_wait_time() < j->get_overall_wait_time();
}

inline bool current_wait_time_comparator (const Event* i, const Event* j) {
	return i->get_bus_wait_time() < j->get_bus_wait_time();
}

class IOScheduler {
public:
	IOScheduler();
	~IOScheduler();
	void init(Ssd*, FtlParent*, Block_manager_parent*, Migrator*);
	void init();
	void schedule_events_queue(deque<Event*> events);
	void schedule_event(Event* events);
	bool is_empty();
	void execute_soonest_events();
	void handle(vector<Event*>& events);
	void handle_noop_events(vector<Event*>& events);
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
    	ar & ssd;
    	ar & ftl;
    	ar & bm;
    	ar & migrator;
    }
    Block_manager_parent* get_bm() { return bm; }
    void set_block_manager(Block_manager_parent* b) {bm = b;}
    Migrator* get_migrator() { return migrator; }
private:
	void setup_structures(deque<Event*> events);
	enum status execute_next(Event* event);
	void execute_current_waiting_ios();
	void handle_event(Event* event);
	void handle_write(Event* event);
	void handle_read(Event* event);
	void handle_flexible_read(Event* event);
	void setup_dependent_event(Event* first, Event* dependent);
	void transform_copyback(Event* event);
	void handle_finished_event(Event *event);
	void remove_redundant_events(Event* new_event);
	bool should_event_be_scheduled(Event* event);
	void init_event(Event* event);
	void push(Event* event);
	void manage_operation_completion(Event* event);
	double get_soonest_event_time(vector<Event*> const& events) const;

	event_queue* future_events;
	Scheduling_Strategy* overdue_events;
	Scheduling_Strategy* current_events;
	event_queue* completed_events;

	map<uint, deque<Event*> > dependencies;

	Ssd* ssd;
	FtlParent* ftl;
	Block_manager_parent* bm;
	Migrator* migrator;

	map<uint, uint> dependency_code_to_LBA;
	map<uint, event_type> dependency_code_to_type;
	map<uint, uint> LBA_currently_executing;
	map<uint, queue<uint> > op_code_to_dependent_op_codes;

	struct Safe_Cache {
		const uint size;
		set<long> logical_addresses;
		inline Safe_Cache(int size) : size(size), logical_addresses() {}
		inline bool has_space() { return logical_addresses.size() < size; }
		inline void insert(long logical_address) { logical_addresses.insert(logical_address); }
		inline void remove(long logical_address) { logical_addresses.erase(logical_address); }
		inline bool exists(long logical_address) { return logical_addresses.count(logical_address) == 1; }
	};

	Safe_Cache safe_cache;

	void update_current_events(double current_time);
	void remove_current_operation(Event* event);
	void promote_to_gc(Event* event_to_promote);
	void make_dependent(Event* dependent_event, uint independent_code);
	double get_current_time() const;
	void try_to_put_in_safe_cache(Event* write);
};

}


#endif /* SCHEDULER_H_ */
