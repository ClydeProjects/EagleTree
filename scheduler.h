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
	Priorty_Scheme(IOScheduler* scheduler) : scheduler(scheduler), queue(NULL) {}
	virtual ~Priorty_Scheme() {};
	virtual void schedule(vector<Event*>& events) = 0;
	void set_queue(event_queue* q) { queue = q; }
protected:
	void seperate_internal_external(vector<Event*> const& events, vector<Event*>& internal, vector<Event*>& external);
	void seperate_by_type(vector<Event*> const& events, vector<Event*>& read_commands, vector<Event*>& copyback_commands, vector<Event*>& writes, vector<Event*>& erases);
	IOScheduler* scheduler;
	event_queue* queue;
};

class Fifo_Priorty_Scheme : public Priorty_Scheme {
public:
	Fifo_Priorty_Scheme(IOScheduler* scheduler)  : Priorty_Scheme(scheduler) {};
	void schedule(vector<Event*>& events);
};

//
class Semi_Fifo_Priorty_Scheme : public Priorty_Scheme {
public:
	Semi_Fifo_Priorty_Scheme(IOScheduler* scheduler)  : Priorty_Scheme(scheduler) {};
	void schedule(vector<Event*>& events);
};

class Noop_Priorty_Scheme : public Priorty_Scheme {
public:
	Noop_Priorty_Scheme(IOScheduler* scheduler)  : Priorty_Scheme(scheduler) {};
	void schedule(vector<Event*>& events);
};

class Re_Er_Wr_Priorty_Scheme : public Priorty_Scheme {
public:
	Re_Er_Wr_Priorty_Scheme(IOScheduler* scheduler)  : Priorty_Scheme(scheduler) {};
	void schedule(vector<Event*>& events);
};

class Er_Wr_Re_gcRe_gcWr_Priorty_Scheme : public Priorty_Scheme {
public:
	Er_Wr_Re_gcRe_gcWr_Priorty_Scheme(IOScheduler* scheduler)  : Priorty_Scheme(scheduler) {};
	void schedule(vector<Event*>& events);
};

class gcRe_gcWr_Er_Re_Wr_Priorty_Scheme : public Priorty_Scheme {
public:
	gcRe_gcWr_Er_Re_Wr_Priorty_Scheme(IOScheduler* scheduler)  : Priorty_Scheme(scheduler) {};
	void schedule(vector<Event*>& events);
};

class We_Re_gcWr_E_gcR_Priorty_Scheme : public Priorty_Scheme {
public:
	We_Re_gcWr_E_gcR_Priorty_Scheme(IOScheduler* scheduler)  : Priorty_Scheme(scheduler) {};
	void schedule(vector<Event*>& events);
};

class Smart_App_Priorty_Scheme : public Priorty_Scheme {
public:
	Smart_App_Priorty_Scheme(IOScheduler* scheduler)  : Priorty_Scheme(scheduler) {};
	void schedule(vector<Event*>& events);
};

class event_queue {
public:
	event_queue() : events(), num_events(0) {};
	virtual ~event_queue();
	virtual void push(Event*, double value);
	virtual void push(Event*);
	vector<Event*> get_soonest_events();
	virtual bool remove(Event*);
	virtual void register_event_compeltion(Event*) {}
	virtual Event* find(long dep_code) const;
	inline bool empty() const { return events.empty(); }
	double get_earliest_time() const { return events.empty() ? 0 : floor((*events.begin()).first); };
	int size() const { return num_events; }
	virtual void print();
private:
	map<long, vector<Event*> > events;
	int num_events;
};

class special_event_queue : public event_queue {
public:
	special_event_queue() : event_queue(), writes(), next_time(INFINITE), earliest(NULL) {};
	virtual ~special_event_queue();
	virtual void push(Event*, double value);
	virtual void push(Event*);
	virtual void register_event_compeltion(Event*);
	vector<Event*> get_soonest_events();
	virtual bool remove(Event*);
	virtual Event* find(long dep_code) const;
	void compute_new_min_time();
	inline bool empty() const { return event_queue::empty() && event_queue::size() == 0 && writes.empty(); }
	double get_earliest_time() const;
	int size() const { return event_queue::size() + writes.size(); }
	void print();
private:
	bool remove_write(Event*);
	double next_time;
	Event* earliest;
	vector<Event*> writes;
};

class Scheduling_Strategy : public event_queue {
public:
	Scheduling_Strategy(IOScheduler* s, Ssd* ssd, Priorty_Scheme* scheme) : event_queue(), scheduler(s), ssd(ssd), priorty_scheme(scheme) {
		priorty_scheme->set_queue(this);
	}
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
	void schedule_event(Event* event);
	bool is_empty();
	void execute_soonest_events();
	void handle(vector<Event*>& events);
	void handle(Event* event);
	void handle_noop_events(vector<Event*>& events);
	void inform_FTL_of_noop_completion(Event* event);
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
	void trigger_next_migration(Event* event);
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
	void send_earliest_completed_events_back();
	void complete(Event* event);

	event_queue* future_events;
	Scheduling_Strategy* overdue_events;
	Scheduling_Strategy* current_events;
	event_queue* completed_events;

	unordered_map<uint, deque<Event*> > dependencies;

	Ssd* ssd;
	FtlParent* ftl;
	Block_manager_parent* bm;
	Migrator* migrator;

	unordered_map<uint, uint> dependency_code_to_LBA;
	unordered_map<uint, event_type> dependency_code_to_type;
	unordered_map<uint, uint> LBA_currently_executing;
	unordered_map<uint, queue<uint> > op_code_to_dependent_op_codes;

	struct Safe_Cache {
		const uint size;
		set<long> logical_addresses;
		inline Safe_Cache(int size) : size(size), logical_addresses() {}
		inline bool has_space() { return logical_addresses.size() < size; }
		inline void insert(long logical_address) { logical_addresses.insert(logical_address); }
		inline void remove(long logical_address) { logical_addresses.erase(logical_address); }
		inline bool exists(long logical_address) { return logical_addresses.count(logical_address) == 1; }
	};

	struct stats {
	public:
		stats();
		void register_IO_completion(Event* e);
		void print();
		struct IO_type_recorder {
			IO_type_recorder();
			void register_io(Event*);
			double get_iterations_per_io();
		private:
			int total_iterations;
			int total_IOs;
		};
	private:
		IO_type_recorder write_recorder;
		IO_type_recorder gc_write_recorder;
		IO_type_recorder gc_read_commands_recorder;
		IO_type_recorder read_commands_recorder;
		IO_type_recorder read_transfers_recorder;
		IO_type_recorder erase_recorder;
	};
	stats stats;

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
