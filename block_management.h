/*
 * block_management.h
 *
 *  Created on: Feb 17, 2013
 *      Author: niv
 */

#ifndef BLOCK_MANAGEMENT_H_
#define BLOCK_MANAGEMENT_H_

#include "ssd.h"
#include "scheduler.h"
//#include "mtrand.h"
namespace ssd {

class Migrator {
public:
	Migrator();
	//Migrator(Migrator&);
	~Migrator();
	void init(IOScheduler*, Block_manager_parent*, Garbage_Collector*, Wear_Leveling_Strategy*, FtlParent*, Ssd*);
	void schedule_gc(double time, int package, int die, int block, int klass);
	vector<deque<Event*> > migrate(Event * gc_event);
	void update_structures(Address const& a, double time);
	void print_pending_migrations();
	deque<Event*> trigger_next_migration(Event * gc_read);
	bool more_migrations(Event * gc_read);
	void register_event_completion(Event* event);
	void register_ECC_check_on(uint logical_address);
	uint how_many_gc_operations_are_scheduled() const;
	void set_block_manager(Block_manager_parent* b) { bm = b; }
	Garbage_Collector* get_garbage_collector() { return gc; }
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
    	ar & scheduler;
    	ar & bm;
    	ar & ssd;
    	ar & ftl;
    	ar & gc;
    	ar & wl;
    }
private:
	bool copy_back_allowed_on(long logical_address);
	void register_copy_back_operation_on(uint logical_address);
	void handle_erase_completion(Event* event);
	void handle_trim_completion(Event* event);
	void issue_erase(Address ra, double time);
	IOScheduler *scheduler;
	Block_manager_parent* bm;
	Ssd* ssd;
	FtlParent* ftl;
	Garbage_Collector* gc;
	Wear_Leveling_Strategy* wl;
	map<long, uint> page_copy_back_count; // Pages that have experienced a copy-back, mapped to a count of the number of copy-backs
	vector<vector<uint> > num_blocks_being_garbaged_collected_per_LUN;
	unordered_map<int, int> blocks_being_garbage_collected;
	vector<queue<Event*> > erase_queue;
	vector<int> num_erases_scheduled_per_package;
	unordered_map<long, vector<deque<Event*> > > dependent_gc;
	unordered_map<Block*, double> gc_time_stat;
};

class Block_manager_parent {
public:
	Block_manager_parent(int classes = 1);
	virtual ~Block_manager_parent();
	virtual void init(Ssd*, FtlParent*, IOScheduler*, Garbage_Collector*, Wear_Leveling_Strategy*, Migrator*);
	virtual void register_write_outcome(Event const& event, enum status status);
	virtual void register_read_command_outcome(Event const& event, enum status status);
	virtual void register_read_transfer_outcome(Event const& event, enum status status);
	virtual void register_erase_outcome(Event& event, enum status status);
	virtual void register_register_cleared();
	virtual Address choose_write_address(Event& write);
	Address choose_flexible_read_address(Flexible_Read_Event* fr);
	virtual void register_write_arrival(Event const& write);
	virtual void trim(Event const& write);
	virtual void receive_message(Event const& message) {}
	double in_how_long_can_this_event_be_scheduled(Address const& die_address, double current_time, event_type type = NOT_VALID) const;
	double soonest_possible_write() const;
	static double soonest_write_time;
	double in_how_long_can_this_write_be_scheduled(double current_time) const;
	double in_how_long_can_this_write_be_scheduled2(double current_time) const;
	void update_next_possible_write_time() const;
	vector<deque<Event*> > migrate(Event * gc_event);
	bool Copy_backs_in_progress(Address const& address);
	bool can_schedule_on_die(Address const& address, event_type type, uint app_io_id) const;
	bool is_die_register_busy(Address const& addr) const;
	void register_trim_making_gc_redundant(Event* trim);
	Address choose_copbyback_address(Event const& write);
	void schedule_gc(double time, int package_id, int die_id, int block, int klass);
	virtual void check_if_should_trigger_more_GC(Event const& event);
	double get_average_migrations_per_gc() const;
	int get_num_age_classes() const { return num_age_classes; }
	int get_num_pages_available_for_new_writes() const { return num_available_pages_for_new_writes; }
	void subtract_from_available_for_new_writes(int num) {
		num_available_pages_for_new_writes -= num;
		//printf("%d   %d\n", num_available_pages_for_new_writes, num_free_pages);
	}
	vector<Block*> const& get_all_blocks() const { return all_blocks; }
	uint sort_into_age_class(Address const& address) const;
	void copy_state(Block_manager_parent* bm);
	virtual bool bm(Block* block, double current_time) { return true; }
	virtual bool may_garbage_collect_this_block(Block* block, double current_time) { return true;}
	static Block_manager_parent* get_new_instance();
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
    	ar & ssd;
    	ar & ftl;
    	ar & scheduler;
    	ar & free_block_pointers;

    	ar & free_blocks;
    	ar & all_blocks;
    	ar & num_age_classes;
    	ar & num_free_pages;
    	ar & num_available_pages_for_new_writes;
    	ar & free_block_pointers;

    	ar & wl;
    	ar & gc;
    	ar & migrator;
    }
    virtual void print() const {}
    Address find_free_unused_block(double time);
	Address find_free_unused_block(uint package_id, uint die_id, enum age age, double time);
	Address find_free_unused_block(uint package_id, uint die_id, double time);
	Address find_free_unused_block(uint package_id, double time);
	Address find_free_unused_block(enum age age, double time);
	pair<bool, pair<int, int> > get_free_block_pointer_with_shortest_IO_queue(vector<vector<Address> > const& dies) const;
	void return_unfilled_block(Address block_address, double current_time, bool give_to_block_pointers);
	int get_num_free_blocks() const;
	void print_free_blocks() const;
protected:
	virtual Address choose_best_address(Event& write) = 0;
	virtual Address choose_any_address(Event const& write) = 0;
	void increment_pointer(Address& pointer);
	bool can_schedule_write_immediately(Address const& prospective_dest, double current_time);
	bool can_write(Event const& write) const;
	Address get_free_block_pointer_with_shortest_IO_queue();

	inline bool has_free_pages(Address const& address) const { return address.valid == PAGE && address.page < BLOCK_SIZE; }

	Ssd* ssd;
	FtlParent* ftl;
	IOScheduler *scheduler;
	vector<vector<Address> > free_block_pointers;
	Migrator* migrator;
	vector<vector<vector<deque<Address> > > > free_blocks;  // package -> die -> class -> list of such free blocks

	int get_num_free_blocks(int package, int die) const;
	int get_num_pointers_with_free_space() const;
	int get_num_available_pages_for_new_writes() const { return num_available_pages_for_new_writes; }
private:
	Address find_free_unused_block(uint package_id, uint die_id, uint age_class, double time);
	void issue_erase(Address a, double time);


	bool copy_back_allowed_on(long logical_address);
	void register_copy_back_operation_on(uint logical_address);
	void register_ECC_check_on(uint logical_address);
	bool schedule_queued_erase(Address location);

	vector<Block*> all_blocks;

	// The num_age_classes variable controls into how many age classes we divide blocks.
	// In every LUN, the block manager tries to keep num_age_classes free blocks.
	// This allows doing efficient dynamic wear-leveling by putting pages of a certain temperature in blocks of a certain age.
	int num_age_classes;

	uint num_free_pages;
	uint num_available_pages_for_new_writes;

	pair<bool, pair<int, int> > last_get_free_block_pointer_with_shortest_IO_queue_result;
	bool IO_has_completed_since_last_shortest_queue_search;

	vector<queue<Event*> > erase_queue;
	vector<int> num_erases_scheduled_per_package;
	Wear_Leveling_Strategy* wl;
	Garbage_Collector* gc;

};



class Wear_Leveling_Strategy {
public:
	Wear_Leveling_Strategy();
	Wear_Leveling_Strategy(Ssd* ssd, Migrator*);
	~Wear_Leveling_Strategy() {};
	void register_erase_completion(Event const& event);
	bool schedule_wear_leveling_op(Block* block);
	double get_normalised_age(uint age) const;
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
    	ar & age_distribution;
    	ar & all_blocks;
    	ar & num_erases_up_to_date;
    	ar & ssd;
    	ar & average_erase_cycle_time;
    	ar & blocks_being_wl;
    	ar & blocks_to_wl;
    	ar & migrator;

    	ar & max_age;
    	ar & block_data;
    }
private:
    void init();
	double get_min_age() const;
	//void update_blocks_with_min_age(uint min_age);
	void find_wl_candidates(double current_time);
	//set<Block*> blocks_with_min_age;
	map<int, int> age_distribution;  // maps block ages to the number of blocks with this age
	vector<Block*> all_blocks;
	int num_erases_up_to_date;
	Ssd* ssd;
	double average_erase_cycle_time;
	set<Block*> blocks_being_wl;
	set<Block*> blocks_to_wl;
	Migrator* migrator;
	int max_age;
	MTRand_int32 random_number_generator;
	struct Block_data {
		int age;
		double last_erase_time;
		Block_data() : age(0), last_erase_time(0) {}
	    friend class boost::serialization::access;
	    template<class Archive>
	    void serialize(Archive & ar, const unsigned int version)
	    {
	    	ar & age;
	    	ar & last_erase_time;
	    }
	};
	vector<Block_data> block_data;
};

// A BM that assigns each write to the die with the shortest queue. No hot-cold seperation
class Block_manager_parallel : public Block_manager_parent {
public:
	Block_manager_parallel();
	~Block_manager_parallel() {}
	void register_write_outcome(Event const& event, enum status status);
	void register_erase_outcome(Event& event, enum status status);
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
    	ar & boost::serialization::base_object<Block_manager_parent>(*this);
    }
protected:
	Address choose_best_address(Event& write);
	Address choose_any_address(Event const& write);
};

// A BM that assigns each write to the die with the shortest queue. No hot-cold seperation
class bm_gc_locality : public Block_manager_parent {
public:
	bm_gc_locality();
	~bm_gc_locality() {}
	void register_write_outcome(Event const& event, enum status status);
	void check_if_should_trigger_more_GC(Event const& event);
	void register_erase_outcome(Event& event, enum status status);
	bool may_garbage_collect_this_block(Block* block, double current_time);
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
    	ar & boost::serialization::base_object<Block_manager_parent>(*this);
    }
protected:
	Address choose_best_address(Event& write);
	Address choose_any_address(Event const& write);
	map<Block*, Address> pointers_for_ongoing_gc_operations;
	vector<vector<queue<Address> > > partially_used_blocks;
private:
	int get_num_partially_empty_blocks() const;
	Address get_block_for_gc(int package, int die, double current_time);
};

// A BM that seperates blocks based on tags
class Block_Manager_Tag_Groups : public Block_manager_parent {
public:
	Block_Manager_Tag_Groups();
	~Block_Manager_Tag_Groups() {}
	void register_write_arrival(Event const& e);
	void register_write_outcome(Event const& event, enum status status);
	void register_erase_outcome(Event& event, enum status status);
	//void increment_pointer_and_find_free(Address& block, double time);
    friend class boost::serialization::access;
    void print() const;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
    	ar & boost::serialization::base_object<Block_manager_parent>(*this);
    	ar & free_block_pointers_tags;
    }
protected:
	Address choose_best_address(Event& write);
	Address choose_any_address(Event const& write);
private:
	map<int, vector<vector<Address> > > free_block_pointers_tags;  // tags, packages, dies
};

struct pointers {
	pointers();
	pointers(Block_manager_parent* bm);
	void register_completion(Event const& e);
	Address get_best_block(Block_manager_parent* bm) const;
	void print() const;
	int get_num_free_blocks() const;
	void retire(double current_time);
	Block_manager_parent* bm;
	vector<vector<Address> > blocks;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
    	ar & bm; ar & blocks;
    }
};

// A simple BM that assigns writes sequentially to dies in a round-robin fashion. No hot-cold separation or anything else intelligent
class Block_manager_roundrobin : public Block_manager_parent {
public:
	Block_manager_roundrobin(bool channel_alternation = true);
	~Block_manager_roundrobin();
	void register_write_outcome(Event const& event, enum status status);
	void register_erase_outcome(Event& event, enum status status);
protected:
	Address choose_best_address(Event& write);
	Address choose_any_address(Event const& write);
private:
	void move_address_cursor();
	Address address_cursor;
	bool channel_alternation;
};

// A BM that assigns each write to the die with the shortest queue, as well as hot-cold seperation
class Shortest_Queue_Hot_Cold_BM : public Block_manager_parent {
public:
	Shortest_Queue_Hot_Cold_BM();
	~Shortest_Queue_Hot_Cold_BM();
	void register_write_outcome(Event const& event, enum status status);
	void register_read_command_outcome(Event const& event, enum status status);
	void register_erase_outcome(Event& event, enum status status);
protected:
	Address choose_best_address(Event& write);
	virtual Address choose_any_address(Event const& write);
	void check_if_should_trigger_more_GC(Event const& event);
private:
	void handle_cold_pointer_out_of_space(double start_time);
	BloomFilter_Page_Hotness_Measurer page_hotness_measurer;
	Address cold_pointer;
};


class Wearwolf : public Block_manager_parent {
public:
	Wearwolf();
	~Wearwolf();
	virtual void register_write_outcome(Event const& event, enum status status);
	virtual void register_read_command_outcome(Event const& event, enum status status);
	virtual void register_erase_outcome(Event& event, enum status status);
protected:
	virtual void check_if_should_trigger_more_GC(Event const&);
	virtual Address choose_best_address(Event& write);
	virtual Address choose_any_address(Event const& write);
	Page_Hotness_Measurer* page_hotness_measurer;
private:
	void handle_cold_pointer_out_of_space(enum read_hotness rh, double start_time);
	void reset_any_filled_pointers(Event const& event);
	Address wcrh_pointer;
	Address wcrc_pointer;
};

class Sequential_Pattern_Detector_Listener {
public:
	virtual ~Sequential_Pattern_Detector_Listener() {}
	virtual void sequential_event_metadata_removed(long key, double current_time) = 0;
};

struct sequential_writes_tracking {
	int counter, num_times_pattern_has_repeated;
	long key;
	double last_arrival_timestamp;
	double const init_timestamp;
	ulong last_io_num;
	sequential_writes_tracking(double time, long key);
};

class Sequential_Pattern_Detector {
public:
	typedef ulong logical_address;
	Sequential_Pattern_Detector(uint threshold);
	~Sequential_Pattern_Detector();
	sequential_writes_tracking const& register_event(logical_address lb, double time);
	void set_listener(Sequential_Pattern_Detector_Listener * listener);
	void remove_old_sequential_writes_metadata(double time);
private:
	map<logical_address, logical_address> sequential_writes_key_lookup;  // a map from the next expected LBA in a seqeuntial pattern to the first LBA, which is the key
	map<logical_address, sequential_writes_tracking*> sequential_writes_identification_and_data;	// a map from the first logical write of a sequential pattern to metadata about the pattern
	sequential_writes_tracking* restart_pattern(int key, double time);
	sequential_writes_tracking* process_next_write(int lb, double time);
	sequential_writes_tracking* init_pattern(int lb, double time);
	uint registration_counter;
	Sequential_Pattern_Detector_Listener* listener;
	uint threshold;
	ulong io_num;
};

class Sequential_Locality_BM : public Block_manager_parallel, public Sequential_Pattern_Detector_Listener {
public:
	Sequential_Locality_BM();
	~Sequential_Locality_BM();
	void register_write_arrival(Event const& write);
	void register_write_outcome(Event const& event, enum status status);
	void register_erase_outcome(Event& event, enum status status);
	void sequential_event_metadata_removed(long key, double current_time);
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
    	ar & boost::serialization::base_object<Block_manager_parent>(*this);
    }
protected:
	Address choose_best_address(Event& write);
	Address choose_any_address(Event const& write);
private:
	enum parallel_degree_for_sequential_files { ONE, LUN, CHANNEL };
	parallel_degree_for_sequential_files parallel_degree;

	struct sequential_writes_pointers {
		int num_pointers;
		vector<vector<Address> > pointers;
		uint cursor;
		int tag;
		sequential_writes_pointers();
	};

	struct tagged_sequential_write {
		int key, size, free_allocated_space, num_written;
		tagged_sequential_write() : key(-1), size(-1), free_allocated_space(0), num_written(0) {}
		tagged_sequential_write(int key, int size) : key(key), size(size), free_allocated_space(0), num_written(0) {}
		bool need_more_space() {	return is_finished() ? false : size > free_allocated_space; }
		bool is_finished() {	return num_written == size; }
	};

	map<long, sequential_writes_pointers> seq_write_key_to_pointers_mapping;

	void set_pointers_for_sequential_write(long key, double time);
	void set_pointers_for_tagged_sequential_write(int tag, double time);
	Address perform_sequential_write(Event& event, long key);
	Address perform_sequential_write_shortest_queue(sequential_writes_pointers& swp);
	Address perform_sequential_write_round_robin(sequential_writes_pointers& swp);
	void process_write_completion(Event const& event, long key, pair<long, long> index);

	Sequential_Pattern_Detector* detector;

	enum strategy {SHOREST_QUEUE, ROUND_ROBIN};
	strategy strat;

	map<long, tagged_sequential_write> tag_map; // maps from tags of sequential writes to the size of the sequential write
	map<long, long> arrived_writes_to_sequential_key_mapping;

	void print_tags(); // to be removed
	MTRand_int32 random_number_generator;
	int num_hits;
	int num_misses;
};

class Garbage_Collector {
public:
	Garbage_Collector() : ssd(NULL), bm(NULL), num_age_classes(1) {}
	Garbage_Collector(Ssd* ssd, Block_manager_parent* bm) : ssd(ssd), bm(bm), num_age_classes(bm->get_num_age_classes()) {}
	virtual ~Garbage_Collector() {}
	virtual void register_event_completion(Event const& event) {};
	virtual Block* choose_gc_victim(int package_id, int die_id, int klass) const = 0;
	virtual void commit_choice_of_victim(Address const& phys_address, double time) = 0;
	void set_block_manager(Block_manager_parent* b) { bm = b; }
	virtual void set_scheduler(IOScheduler*) {}
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
    	ar & ssd;
    	ar & bm;
    	ar & num_age_classes;
    }
protected:
	Ssd* ssd;
	Block_manager_parent* bm;
	int num_age_classes;
};

// The garbage collector organizes blocks in a data structure that is convenient for choosing which block to garbage-collect next
// This organization happens within the gc_candidates structure.
// Blocks that are candidates for garbage collection are first organized based on which package and die they belong to.
// Within the each die, they are further divided how old they are (i.e. how many erases they have experienced).
// The variable num_age_classes controls how many groups we use for blocks of different ages.
// Note that the block manager maintains a free block for every age class in every LUN. This allows us to implement efficient
// dynamic wear-leveling by putting pages of a certain temperature in blocks of a certain age.
class Garbage_Collector_Greedy : public Garbage_Collector {
public:
	Garbage_Collector_Greedy();
	Garbage_Collector_Greedy(Ssd* ssd, Block_manager_parent* bm);
	// Called by the block manager after any page in the SSD is invalidated, as a result of a trim or a write.
	// This is used to keep the gc_candidates structure updated.
	virtual void register_event_completion(Event const& event);

	// Called by the block manager to ask the garbage-collector for a good block to garbage-collect in a given package, die, and with a certain age.
	Block* choose_gc_victim(int package_id, int die_id, int klass) const;
	// Called by the block manager when a GC operation for a certain block has been issued. This block is removed from the gc_candidates structure.
	void commit_choice_of_victim(Address const& phys_address, double time);
	friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
    	ar & boost::serialization::base_object<Garbage_Collector>(*this);
    	ar & gc_candidates;
    }
private:
	vector<long> get_relevant_gc_candidates(int package_id, int die_id, int klass) const;
	vector<vector<set<long> > > gc_candidates;
};

class Garbage_Collector_LRU : public Garbage_Collector {
public:
	Garbage_Collector_LRU();
	Garbage_Collector_LRU(Ssd* ssd, Block_manager_parent* bm);
	virtual void register_event_completion(Event const& event);
	Block* choose_gc_victim(int package_id, int die_id, int klass) const;
	void commit_choice_of_victim(Address const& phys_address, double time);
	friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
    	ar & boost::serialization::base_object<Garbage_Collector>(*this);
    	ar & gc_candidates;
    }
private:
	vector<vector<queue<int> > > gc_candidates;  // for each die, a queue of blocks to be erased
};

class flash_resident_ftl_garbage_collection : public Garbage_Collector {
public:
	flash_resident_ftl_garbage_collection(Ssd* ssd, Block_manager_parent* bm) : Garbage_Collector(ssd, bm), ftl(NULL) {}
	flash_resident_ftl_garbage_collection() : Garbage_Collector(), ftl(NULL) {}
	virtual void invalid_address_notification(Address const& a, double time) = 0;
	virtual void set_ftl(flash_resident_page_ftl* new_ftl) { new_ftl = ftl; }
protected:
	flash_resident_page_ftl* ftl;

};

struct logarithmic_gecko_index_entry {
	vector<bool> bitmap;
	bool erase_flag;
	void print() const;
};

struct logarithmic_gecko_cached_entry {
	logarithmic_gecko_cached_entry();
	vector<bool> bitmap;
	bool up_to_date;
	void print() const;
};

enum write_amp_choice {greedy, prob, opt};

struct group_def {
	group_def(double update_frequency, double size, int tag = UNDEFINED) : update_frequency(update_frequency), size(size), tag(tag) {}
	group_def() : update_frequency(), size(), tag() {}
	double update_frequency;
	double size;
	int tag;
    friend class boost::serialization::access;
    template<class Archive> void serialize(Archive & ar, const unsigned int version) {
    	ar & update_frequency;
    	ar & size;
    	ar & tag;
    }
};

class Groups_Message : public Message {
public:
	Groups_Message(double time) : Message(time), redistribution_of_update_frequencies(false) {}
	bool redistribution_of_update_frequencies;
	vector<group_def> groups; // one pair for each group. The first double is the update frequency, between 0 and 1, and the second is the group size as a fraction of the whole
};

class group {
public:
	group(double prob, double size, Block_manager_parent* bm, Ssd* ssd, int index);
	group();
	void print() const;
	void print_die_spesific_info() const;
	void print_tags_per_group() const;
	void print_blocks_valid_pages_per_die() const;
	void print_blocks_valid_pages() const;
	double get_prob_op(double PBA, double LBA);
	double get_greedy_op(double PBA, double LBA);
	double get_average_op(double PBA, double LBA);
	double get_write_amp(write_amp_choice choice) const;
	void register_write_outcome(Event const& event);
	void register_erase_outcome(Event& event);
	Block* get_gc_victim_LRU(int package, int die) const;
	Block* get_gc_victim_window_greedy(int package, int die) const;
	inline double get_normalized_hits_per_page() const { return (prob / num_pages) * OVER_PROVISIONING_FACTOR * NUMBER_OF_ADDRESSABLE_PAGES(); }
	Block* get_gc_victim_greedy(int package, int die) const;
	bool is_starved() const;
	void accept_block(Address block_addr);
	bool needs_more_blocks() const;
	int needs_how_many_blocks() const;
	bool in_equilbirium() const;
	void retire_active_blocks(double current_time);
	static bool in_total_equilibrium(vector<group> const& groups, int group_id);
	static double get_average_write_amp(vector<group>& groups, write_amp_choice choice = opt);
	static vector<group> allocate_op(vector<group> const& groups);
	static vector<group> closed_form_method(vector<group> const& groups);
	static vector<group> closed_form_method(vector<group> const& groups, int LBA, int PBA);
	static vector<group> iterate(vector<group> const& groups);
	static vector<group> iterate_except_first(vector<group> const& groups);
	static void print(vector<group> const& groups);
	static void print_tags_distribution(vector<group> const& groups);
	static void init_stats(vector<group>& groups);
	static void count_num_groups_that_need_more_blocks(vector<group> const& groups);
	double get_avg_pages_per_block_per_die() const;
	double get_avg_pages_per_die() const;
	double get_avg_blocks_per_die() const;
	double get_min_pages_per_die() const;

	double prob, size, offset, OP, OP_greedy, OP_prob, OP_average, actual_prob;
	pointers free_blocks, next_free_blocks;
	set<Block*> block_ids, blocks_being_garbage_collected;
	vector<vector<int> > num_pages_per_die, num_blocks_per_die, num_blocks_ever_given;
	vector<vector<vector<Block*> > > blocks_queue_per_die;
	struct group_stats {
		group_stats() : num_gc_in_group(0), num_writes_to_group(0), num_gc_writes_to_group(0),
				num_requested_gc(0), num_requested_gc_to_balance(0), num_requested_gc_starved(0),
				migrated_in(0), migrated_out(0) {}
		int num_gc_in_group, num_writes_to_group, num_gc_writes_to_group;
		int num_requested_gc, num_requested_gc_to_balance, num_requested_gc_starved;
		int migrated_in, migrated_out;
		void print() const;
	};
	long num_app_writes;
	int num_pages;
	group_stats stats;
	static vector<int> mapping_pages_to_groups;
	static vector<int> mapping_pages_to_tags;
	static int num_groups_that_need_more_blocks, num_groups_that_need_less_blocks;

	StatisticsGatherer stats_gatherer;
	int index;
	int id;
	static int id_generator;
	static int overprov_allocation_strategy;
	Ssd* ssd;
	static int num_writes_since_last_regrouping;
	static bool is_stable();
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
    	ar & prob; ar & size; ar & offset; ar & OP; ar & OP_greedy; ar & OP_prob; ar & OP_average; ar & actual_prob;
    	ar & free_blocks; ar & next_free_blocks; ar & block_ids; ar & blocks_being_garbage_collected;
    	ar & num_pages_per_die; ar & num_blocks_per_die; ar & num_blocks_ever_given; ar & blocks_queue_per_die;
    	ar & num_pages; ar & mapping_pages_to_groups; ar & mapping_pages_to_tags; ar & index; ar & id;
    	ar & num_writes_since_last_regrouping; ar & ssd;
    }
};

// A temperature detector interface to be used by Block_Manager_Groups
class temperature_detector {
public:
	temperature_detector(vector<group>& groups) : groups_demo(), groups(groups) {};
	temperature_detector() : groups_demo(), groups(groups_demo) {}
	virtual ~temperature_detector() {}
	virtual int which_group_should_this_page_belong_to(Event const& event) = 0;
	virtual void register_write_completed(Event const& event, int prior_group, int group_id) { }
	virtual void change_in_groups(vector<group>& groups, double current_time) {}
	template<class Archive> void serialize(Archive & ar, const unsigned int version)
	{
		ar & groups;
		ar & groups_demo;
	}
protected:
	vector<group>& groups;
private:
	vector<group> groups_demo;
};

// Conceptually and oracle that uses tags on a page to infer which group the page belongs to
class tag_detector : public temperature_detector {
public:
	tag_detector(vector<group>& groups) : temperature_detector(groups) {};
	tag_detector() : temperature_detector() {}
 	int which_group_should_this_page_belong_to(Event const& event);
	template<class Archive> void serialize(Archive & ar, const unsigned int version)
	{ ar & boost::serialization::base_object<temperature_detector>(*this); }
};



// A BM that seperates blocks based on tags
class Block_Manager_Groups : public Block_manager_parent {
public:
	Block_Manager_Groups();
	~Block_Manager_Groups();
	void init(Ssd*, FtlParent*, IOScheduler*, Garbage_Collector*, Wear_Leveling_Strategy*, Migrator*);
	void init_detector();
	void register_write_arrival(Event const& e);
	void register_write_outcome(Event const& event, enum status status);
	void register_erase_outcome(Event& event, enum status status);
	bool trigger_gc_in_same_lun_but_different_group(int package, int die, int group_id, double time);
	void handle_block_out_of_space(Event const& event, int group_id);
	void receive_message(Event const& message);
	void change_update_frequencies(Groups_Message const& message);
	void check_if_should_trigger_more_GC(Event const&);
	void try_to_allocate_block_to_group(int group_id, int package, int die, double time);
	bool may_garbage_collect_this_block(Block* block, double current_time);
	void register_logical_address(Event const& event, int group_id);
    void print() const;
    void add_group(double starting_prob_val = 0);
    friend class boost::serialization::access;
    template<class Archive> void serialize(Archive & ar, const unsigned int version)
    {
    	ar & boost::serialization::base_object<Block_manager_parent>(*this);
    	ar & groups;
    	ar & detector;
    }
    static int detector_type;
    static int reclamation_threshold;
    static bool prioritize_groups_that_need_blocks;
    static int garbage_collection_policy_within_groups; // 0 for LRU, 1 for greedy
protected:
	Address choose_best_address(Event& write);
	Address choose_any_address(Event const& write);
private:
	void give_block_to_group(int package, int die, int group_id, double current_time);
	void request_gc(int group_id, int package, int die, double time);
	vector<group> groups;
	struct statistics {
		statistics() : num_group_misses(0),
				num_starved_gc_operations_requested(0), num_normal_gc_operations_requested(0) {}
		int num_group_misses;
		int num_starved_gc_operations_requested;
		int num_normal_gc_operations_requested;
	};
	statistics stats;
	temperature_detector* detector;
};

// Conceptually and oracle that uses tags on a page to infer which group the page belongs to
class bloom_detector : public temperature_detector {
public:
	bloom_detector(vector<group>& groups, Block_Manager_Groups* bm);
	bloom_detector();
	virtual ~bloom_detector() {};
	virtual int which_group_should_this_page_belong_to(Event const& event);
	virtual void change_in_groups(vector<group>& groups, double current_time);
	virtual void register_write_completed(Event const& event, int prior_group, int new_group_id);
    template<class Archive> void serialize(Archive & ar, const unsigned int version)
    {
    	ar & boost::serialization::base_object<temperature_detector>(*this);
    	ar & data; ar & bm; ar & current_interval_counter;
    	ar & interval_size_of_the_lba_space; ar & highest_group; ar & lowest_group;
    }
    static int num_filters;
    static int max_num_groups;
    static int min_num_groups;
    static double bloom_false_positive_probability;
protected:
	int get_interval_length() { return NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR * interval_size_of_the_lba_space; }
	virtual void update_probilities(double current_time) = 0;
	void group_interval_finished(int group_id);
	struct group_data {
	public:
		group_data(group const& group_ref, vector<group>& data);
		group_data();
		vector<bloom_filter*> filters;
		int in_filters(Event const& );

		int bloom_filter_hits;
		int interval_hit_count;
		inline double get_hits_per_page() const { return groups[index].prob / groups[index].num_pages; }

		inline group get_group() { return groups[index]; }
		int index;
		int lower_group_id, upper_group_id;
		int age_in_intervals, age_in_group_periods;
		vector<group>& groups;
	    template<class Archive> void serialize(Archive & ar, const unsigned int version)
	    {
	    	//ar & current_filter; ar & filter2; ar & filter3;
	    	ar & bloom_filter_hits; ar & interval_hit_count;
	    	ar & index; ar & lower_group_id; ar & upper_group_id; ar & age_in_intervals;
	    	ar & groups;
	    }
	private:
	    vector<group> groups_none;
	};
	bool create_higher_group(int index) const;
	void try_to_merge_groups(double current_time);
	void merge_groups(group_data* gd1, group_data* gd2, double current_time);
	vector<group_data*> data;	// sorted by group update probability
	Block_Manager_Groups* bm;
	int current_interval_counter;
	double interval_size_of_the_lba_space;
	group_data* highest_group, *lowest_group;
	vector<int> tag_map;
private:
	void change_id_for_pages(int old_id, int new_id);
};

class adaptive_bloom_detector : public bloom_detector {
public:
	adaptive_bloom_detector(vector<group>& groups, Block_Manager_Groups* bm) : bloom_detector(groups, bm) { update_probilities(0); }
	void update_probilities(double current_time);
	virtual void adjust_groups(double current_time);
};

class non_adaptive_bloom_detector : public bloom_detector {
public:
	non_adaptive_bloom_detector(vector<group>& groups, Block_Manager_Groups* bm);
	void change_in_groups(vector<group>& groups, double current_time);
	void update_probilities(double current_time);
private:
	vector<int> hit_rate;
};

class tag_based_with_prob_recomp : public adaptive_bloom_detector {
public:
	tag_based_with_prob_recomp(vector<group>& groups, Block_Manager_Groups* bm) : adaptive_bloom_detector(groups, bm) { update_probilities(0); }
	virtual int which_group_should_this_page_belong_to(Event const& event);
	void adjust_groups(double current_time) {}
};



}
#endif /* BLOCK_MANAGEMENT_H_ */
