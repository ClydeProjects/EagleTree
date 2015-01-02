/*
 * Operating_System.h
 *
 *  Created on: Feb 19, 2013
 *      Author: niv
 */

#ifndef OPERATING_SYSTEM_H_
#define OPERATING_SYSTEM_H_

#include "ssd.h"
#include "scheduler.h"

namespace ssd {

/*
 *
 */
class Thread
{
public:
	Thread();
	virtual ~Thread();
	void init(OperatingSystem* os, double time);
	void register_event_completion(Event* event);
	Event* peek() const;
	Event* pop();

	bool is_finished() const;
	int get_num_ongoing_IOs() const { return num_IOs_executing; }
	void stop();
	bool is_stopped() const;

	inline void set_time(double current_time) { time = current_time; }
	inline double get_time() { return time; }
	inline void add_follow_up_thread(Thread* thread) { threads_to_start_when_this_thread_finishes.push_back(thread); }
	inline void add_follow_up_threads(vector<Thread*> threads) { threads_to_start_when_this_thread_finishes.insert(threads_to_start_when_this_thread_finishes.end(), threads.begin(), threads.end()); }
	inline vector<Thread*>& get_follow_up_threads() { return threads_to_start_when_this_thread_finishes; }
	StatisticsGatherer* get_internal_statistics_gatherer() { return internal_statistics_gatherer; }
	StatisticsGatherer* get_external_statistics_gatherer() { return external_statistics_gatherer; }
	void set_statistics_gatherer(StatisticsGatherer* new_statistics_gatherer);
	void set_finished() { finished = true; }
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version) {
    	ar & threads_to_start_when_this_thread_finishes;
    }
    static void set_record_internal_statistics(bool val) { record_internal_statistics = val; }
protected:
	virtual void issue_first_IOs() = 0;
	virtual void handle_event_completion(Event* event) = 0;
	virtual void handle_no_IOs_left() {}
	inline double get_current_time() { return time; }
	vector<Thread*> threads_to_start_when_this_thread_finishes;
	OperatingSystem* os;
	void submit(Event* event);
	bool can_submit_more() const;

private:
	double time;
	StatisticsGatherer* internal_statistics_gatherer;
	StatisticsGatherer* external_statistics_gatherer;
	int num_IOs_executing;
	queue<Event*> io_queue;
	bool finished;
	bool stopped;
	static bool record_internal_statistics;
};

/*
 * Class Heirarchy for generating IO types.
 */
class IO_Mode_Generator {
public:
	virtual ~IO_Mode_Generator() {};
	virtual event_type next() = 0;
	virtual void init() {}
    friend class boost::serialization::access;
    template<class Archive> void serialize(Archive & ar, const unsigned int version) {}
};

// Generates writes
class WRITES : public IO_Mode_Generator {
public:
	~WRITES() {};
	event_type next() { return WRITE; };
    friend class boost::serialization::access;
    template<class Archive> void serialize(Archive & ar, const unsigned int version) {
    	ar & boost::serialization::base_object<IO_Mode_Generator>(*this);
    }
};

// Generates trims
class TRIMS : public IO_Mode_Generator {
public:
	~TRIMS() {};
	event_type next() { return TRIM; };
    friend class boost::serialization::access;
    template<class Archive> void serialize(Archive & ar, const unsigned int version) {
    	ar & boost::serialization::base_object<IO_Mode_Generator>(*this);
    }
};

// Generates reads
class READS : public IO_Mode_Generator {
public:
	~READS() {};
	event_type next() { return READ; };
    friend class boost::serialization::access;
    template<class Archive> void serialize(Archive & ar, const unsigned int version) {
    	ar & boost::serialization::base_object<IO_Mode_Generator>(*this);
    }
};

// Generates reads or writes with a certain probability
class READS_OR_WRITES : public IO_Mode_Generator {
public:
	READS_OR_WRITES() : random_number_generator(4624626), write_probability(0.5) {}
	READS_OR_WRITES(ulong seed, double write_probability) :
		random_number_generator(seed), write_probability(write_probability) {}
	~READS_OR_WRITES() {};
	virtual void init() {  }
	event_type next() { return random_number_generator() <= write_probability ? WRITE : READ; };
    friend class boost::serialization::access;
    template<class Archive> void serialize(Archive & ar, const unsigned int version) {
    	ar & boost::serialization::base_object<IO_Mode_Generator>(*this);
    	ar & write_probability;
    	ar & random_number_generator;
    }
private:
    double write_probability;
	MTRand_open random_number_generator;
};

/*
 * Class Heirarchy for generating IO patterns on logical addresses
 */
class IO_Pattern
{
public:
	IO_Pattern() : min_LBA(0), max_LBA(0) {}
	IO_Pattern(long min_LBA, long max_LBA) : min_LBA(min_LBA), max_LBA(max_LBA) {};
	virtual ~IO_Pattern() {};
	virtual int next() = 0;
	long min_LBA, max_LBA;
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version) {
    	ar & min_LBA;
    	ar & max_LBA;
    }
};

// Creates a uniformly randomly distributed IO pattern acress the target logical address space
class Random_IO_Pattern : public IO_Pattern
{
public:
	Random_IO_Pattern() : IO_Pattern(), random_number_generator(23623620) {}
	Random_IO_Pattern(long min_LBA, long max_LBA, ulong seed) : IO_Pattern(min_LBA, max_LBA), random_number_generator(seed) {};
	~Random_IO_Pattern() {};
	int next() { return min_LBA + random_number_generator() % (max_LBA - min_LBA + 1); };
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version) {
    	ar & boost::serialization::base_object<IO_Pattern>(*this);
    	ar & random_number_generator;
    }
private:
	MTRand_int32 random_number_generator;
};

// Creates a uniformly randomly distributed IO pattern acress the target logical address space
class Random_IO_Pattern_Collision_Free : public IO_Pattern
{
public:
	Random_IO_Pattern_Collision_Free() : IO_Pattern(), random_number_generator(23623620), counter(0) {}
	Random_IO_Pattern_Collision_Free(long min_LBA, long max_LBA, ulong seed);
	~Random_IO_Pattern_Collision_Free() {};
	void reinit();
	int next();
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version) {
    	ar & boost::serialization::base_object<IO_Pattern>(*this);
    	ar & random_number_generator;
    }
private:
	MTRand_int32 random_number_generator;
	vector<int> candidates;
	int counter;
};

// Creates a uniformly randomly distributed IO pattern acress the target logical address space
class Sequential_IO_Pattern : public IO_Pattern
{
public:
	Sequential_IO_Pattern() : IO_Pattern(), counter(0) {}
	Sequential_IO_Pattern(long min_LBA, long max_LBA) : IO_Pattern(min_LBA, max_LBA), counter(min_LBA - 1) {};
	~Sequential_IO_Pattern() {};
	int next() { return counter == max_LBA ? counter = min_LBA : ++counter; };
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version) {
    	ar & boost::serialization::base_object<IO_Pattern>(*this);
    	ar & counter;
    }
private:
	long counter;
};

class File_Reading_Thread : public Thread {
public:
	File_Reading_Thread();
	~File_Reading_Thread();
	void issue_first_IOs();
	void read_and_submit();
	int get_next();
	void get_back_to_start_after_init();
	void handle_event_completion(Event* event);
	void print_trace_stats();
	void calc_update_distances();
	void sequentiality_analyze();
	void process_trace();
private:
	string file_name;
	ifstream file;
	set<long> unique_addresses, initial_addresses;
	vector<int> logical_address_space_size_over_time;  // index is size. value is io num
	long io_num;
	bool collect_stats;
	vector<int> address_map;
	vector<int> update_frequency_count;
	vector<int> trace;
	int highest_address;
};

/*
 * Class Heirarchy for creating synthetic IO patterns with many configurable properties.
 */
class Simple_Thread : public Thread
{
public:
	Simple_Thread() : io_gen(NULL), io_type_gen(NULL), number_of_times_to_repeat(0), MAX_IOS(0), io_size(1) {}
	Simple_Thread(IO_Pattern* generator, int MAX_IOS, IO_Mode_Generator* type);
	Simple_Thread(IO_Pattern* generator, IO_Mode_Generator* type, int MAX_IOS, long num_IOs);
	virtual ~Simple_Thread();
	void generate_io();
	void issue_first_IOs();
	void handle_event_completion(Event* event);
	void set_io_size(int size) { io_size = size; }
	inline void set_num_ios(ulong num_ios) { number_of_times_to_repeat = num_ios; }
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version) {
    	ar & boost::serialization::base_object<Thread>(*this);
    	ar & number_of_times_to_repeat;
    	ar & MAX_IOS;
    	ar & io_gen;
    	ar & io_type_gen;
    	ar & io_size;
    }
private:
	long number_of_times_to_repeat;
	int MAX_IOS;
	IO_Pattern* io_gen;
	IO_Mode_Generator* io_type_gen;
	int io_size; // in pages
};



// This thread performs synchronous random writes across the target address space
class Synchronous_Random_Writer : public Simple_Thread
{
public:
	Synchronous_Random_Writer() : Simple_Thread() {}
	Synchronous_Random_Writer(long min_LBA, long max_LBA, ulong randseed)
		: Simple_Thread(new Random_IO_Pattern(min_LBA, max_LBA, randseed), new WRITES(), 1, INFINITE) {}
    friend class boost::serialization::access;
    template<class Archive> void serialize(Archive & ar, const unsigned int version) {
    	ar & boost::serialization::base_object<Simple_Thread>(*this);
    }
};


// This thread performs synchronous random writes across the target address space
class Synchronous_No_Collision_Random_Writer : public Simple_Thread
{
public:
	Synchronous_No_Collision_Random_Writer() : Simple_Thread() {}
	Synchronous_No_Collision_Random_Writer(long min_LBA, long max_LBA, ulong randseed)
		: Simple_Thread(new Random_IO_Pattern_Collision_Free(min_LBA, max_LBA, randseed), new WRITES(), 1, INFINITE) {}
};

// This thread performs synchronous random reads across the target address space
class Synchronous_Random_Reader : public Simple_Thread
{
public:
	Synchronous_Random_Reader(long min_LBA, long max_LBA, ulong randseed )
		: Simple_Thread(new Random_IO_Pattern(min_LBA, max_LBA, randseed), new READS(), 1, INFINITE) {}
};

// This thread performs asynchronous random writes across the target address space
class Asynchronous_Random_Writer : public Simple_Thread
{
public:
	Asynchronous_Random_Writer() : Simple_Thread() {}
	Asynchronous_Random_Writer(long min_LBA, long max_LBA, ulong randseed)
		: Simple_Thread(new Random_IO_Pattern(min_LBA, max_LBA, randseed), new WRITES(), MAX_SSD_QUEUE_SIZE * 2, INFINITE) {}
    friend class boost::serialization::access;
    template<class Archive> void serialize(Archive & ar, const unsigned int version) {
    	ar & boost::serialization::base_object<Simple_Thread>(*this);
    }
};

// This thread performs asynchronous random reads across the target address space
class Asynchronous_Random_Reader : public Simple_Thread
{
public:
	Asynchronous_Random_Reader() : Simple_Thread() {}
	Asynchronous_Random_Reader(long min_LBA, long max_LBA, ulong randseed)
		: Simple_Thread(new Random_IO_Pattern(min_LBA, max_LBA, randseed), new READS(), MAX_SSD_QUEUE_SIZE * 2, INFINITE) {}
    friend class boost::serialization::access;
    template<class Archive> void serialize(Archive & ar, const unsigned int version) {
    	ar & boost::serialization::base_object<Simple_Thread>(*this);
    }
};

// This thread performs synchronous sequential writes across the target address space
class Synchronous_Sequential_Writer : public Simple_Thread
{
public:
	Synchronous_Sequential_Writer(long min_LBA, long max_LBA )
		: Simple_Thread(new Sequential_IO_Pattern(min_LBA, max_LBA), 1, new WRITES()) {}
};

// This thread performs asynchronous sequential writes across the target address space
class Asynchronous_Sequential_Writer : public Simple_Thread
{
public:
	Asynchronous_Sequential_Writer() : Simple_Thread() {}
	Asynchronous_Sequential_Writer(long min_LBA, long max_LBA)
		: Simple_Thread(new Sequential_IO_Pattern(min_LBA, max_LBA), MAX_SSD_QUEUE_SIZE * 2, new WRITES()) {
	}
    friend class boost::serialization::access;
    template<class Archive> void serialize(Archive & ar, const unsigned int version) {
    	ar & boost::serialization::base_object<Simple_Thread>(*this);
    }
};

// This thread trims the target address space
class Asynchronous_Sequential_Trimmer : public Simple_Thread
{
public:
	Asynchronous_Sequential_Trimmer(long min_LBA, long max_LBA)
		: Simple_Thread(new Sequential_IO_Pattern(min_LBA, max_LBA), MAX_SSD_QUEUE_SIZE * 2, new TRIMS()) {
	}
};

// This thread synchronously and sequentially reads the target address space
class Synchronous_Sequential_Reader : public Simple_Thread
{
public:
	Synchronous_Sequential_Reader(long min_LBA, long max_LBA )
		: Simple_Thread(new Sequential_IO_Pattern(min_LBA, max_LBA), 1, new READS()) {}
};

// This thread asynchronously and sequentially reads the target address space
class Asynchronous_Sequential_Reader : public Simple_Thread
{
public:
	Asynchronous_Sequential_Reader(long min_LBA, long max_LBA )
		: Simple_Thread(new Sequential_IO_Pattern(min_LBA, max_LBA), MAX_SSD_QUEUE_SIZE * 2, new READS()) {}
};

// This thread performs random reads and writes
class Asynchronous_Random_Reader_Writer : public Simple_Thread
{
public:
	Asynchronous_Random_Reader_Writer(long min_LBA, long max_LBA, ulong seed, double writes_probability = 0.5 )
		: Simple_Thread(new Random_IO_Pattern(min_LBA, max_LBA, seed), new READS_OR_WRITES(seed, writes_probability), MAX_SSD_QUEUE_SIZE * 2, INFINITE) {}
    friend class boost::serialization::access;
    template<class Archive> void serialize(Archive & ar, const unsigned int version) {
    	ar & boost::serialization::base_object<Simple_Thread>(*this);
    }
};


// This thread simulates the IO pattern of an external sort algorithm
class External_Sort : public Thread
{
public:
	External_Sort(long relation_min_LBA, long relation_max_LBA, long RAM_available,
			long free_space_min_LBA, long free_space_max_LBA);
	void issue_first_IOs();
	Event* execute_first_phase();
	Event* execute_second_phase();
	Event* execute_third_phase();
	void handle_event_completion(Event* event);
private:
	long relation_min_LBA, relation_max_LBA, RAM_available, free_space_min_LBA, free_space_max_LBA, cursor, counter, number_finished;
	int num_partitions, num_pages_in_last_partition;
	enum external_sort_phase {FIRST_PHASE_READ, FIRST_PHASE_WRITE, SECOND_PHASE, THIRD_PHASE, FINISHED};
	external_sort_phase phase;
	bool can_start_next_read;
};

// A thread that simulates the IO pattern of a grace hash join between two relations
class Grace_Hash_Join : public Thread
{
public:
	Grace_Hash_Join(long relation_A_min_LBA,        long relation_A_max_LBA,
					long relation_B_min_LBA,        long relation_B_max_LBA,
					long free_space_min_LBA,        long free_space_max_LBA,
					bool use_flexible_reads = true, bool use_tagging  = true,
					long rows_per_page      = 32,    int ranseed = 72);
	void issue_first_IOs();

	void handle_event_completion(Event* event);
	void static initialize_counter() { printf("grace_counter: %d\n", grace_counter); grace_counter = 0; };
	int get_counter() { return grace_counter; };
private:
	static int grace_counter;
	void execute_build_phase();

	void execute_probe_phase();
	void setup_probe_run();
	void read_smaller_bucket();
	void trim_smaller_bucket();
	void read_next_in_larger_bucket();

	void create_flexible_reader(int start, int finish);
	void handle_read_completion_build();
	void flush_buffer(int buffer_id);

	long relation_A_min_LBA, relation_A_max_LBA;
	long relation_B_min_LBA, relation_B_max_LBA;
	long free_space_min_LBA, free_space_max_LBA;
	bool use_flexible_reads, use_tagging;
	long input_cursor;
	long relation_A_size, relation_B_size, free_space_size;
	long rows_per_page;
	int num_partitions, partition_size;

	// Bookkeeping variables
	Flexible_Reader* flex_reader;
	MTRand_int32 random_number_generator;
	enum {BUILD, PROBE_SYNCH, PROBE_ASYNCH, DONE} phase;
	vector<int> output_buffers;
	vector<int> output_cursors;
	vector<int> output_cursors_startpoints;
	vector<int> output_cursors_splitpoints; // Separates relation A and B in output buffers
	int victim_buffer;
	int writes_in_progress, reads_in_progress;
	set<long> reads_in_progress_set;
	int small_bucket_begin, small_bucket_end;
	int large_bucket_begin, large_bucket_cursor, large_bucket_end;
	bool finished_reading_smaller_bucket, finished_trimming_smaller_bucket;

};

struct Address_Range {
	Address_Range() : min(0), max(0) {}
	long min;
	long max;
	Address_Range(long min, long max);
	long get_size() { return max - min + 1; }
	Address_Range split(long num_pages_in_new_part) {
		Address_Range new_range(min, min + num_pages_in_new_part - 1);
		min += num_pages_in_new_part;
		return new_range;
	}
	bool is_contiguously_followed_by(Address_Range other) const;
	bool is_followed_by(Address_Range other) const;
	void merge(Address_Range other);
    friend class boost::serialization::access;
    template<class Archive> void
    serialize(Archive & ar, const unsigned int version) {
    	ar & min;
    	ar & max;
    }
};

// This is a file manager that writes one file at a time sequentially
// files might be fragmented across logical space
// files have a random size determined by the file manager
// fragmentation will eventually happen
class File_Manager : public Thread
{
public:
	File_Manager();
	File_Manager(long min_LBA, long max_LBA, uint num_files_to_write, long max_file_size, ulong randseed = 0);
	~File_Manager();
	void issue_first_IOs();
	void handle_event_completion(Event* event);
	void handle_no_IOs_left();
	virtual void print_thread_stats();
    friend class boost::serialization::access;
    template<class Archive> void
    serialize(Archive & ar, const unsigned int version) {
    	ar & boost::serialization::base_object<Thread>(*this);
    	ar & num_free_pages;
    	ar & free_ranges;
    	ar & live_files;
    	ar & min_LBA;
    	ar & max_LBA;
    	ar & num_files_to_write;
    	ar & max_file_size;
    	ar & random_number_generator;
    	ar & double_generator;
    }
private:
	struct File {
		File() : death_probability(0.5), time_created(0), time_finished_writing(UNDEFINED), time_deleted(UNDEFINED),
				size(0), id(UNDEFINED), num_pages_written(0), ranges_comprising_file() {}
		double death_probability;
		double time_created, time_finished_writing, time_deleted;
		uint size;
		int id;
		uint num_pages_written;
		deque<Address_Range > ranges_comprising_file;
		set<pair<int, int>> on_how_many_luns_is_this_file;
		File(uint size, double death_probability, double time_created, int id);

		bool is_finished() const;
		void register_write_completion(Event* event);
		void finish(double time_finished);
	    friend class boost::serialization::access;
	    template<class Archive> void
	    serialize(Archive & ar, const unsigned int version) {
	    	ar & death_probability;
	    	ar & size;
	    	ar & id;
	    	ar & num_pages_written;
	    	ar & ranges_comprising_file;
	    }
	};

	void schedule_to_trim_file(File* file);
	//double generate_death_probability();
	void write_next_file();
	Address_Range assign_new_range(int num_pages_left_to_allocate);
	void randomly_delete_files();
	//Event* issue_trim();
	void issue_write(long lba);
	void reclaim_file_space(File* file);
	void delete_file(File* victim);
	void handle_file_completion();
	enum phase {TRIM_PHASE, WRITE_PHASE};
	void run_deletion_routine();

	long num_free_pages;
	deque<Address_Range> free_ranges;
	vector<File*> live_files;
	vector<File*> files_history;
	File* current_file;
	long min_LBA, max_LBA;
	int num_files_to_write;
	int file_id_generator;

	MTRand_int32 random_number_generator;
	MTRand_open double_generator;
	long max_file_size;
	int num_pending_trims;
	phase phase;
};

class Flexible_Reader {
public:
	Flexible_Reader(FtlParent const& ftl, vector<Address_Range>);
	Event* read_next(double start_time);
	void register_read_commencement(Flexible_Read_Event*);
	inline bool is_finished() { return finished_counter == 0; }
	inline uint get_num_reads_left() { return finished_counter; }
	inline vector<vector<Address> > const& get_immediate_candidates()     { return immediate_candidates_physical_addresses; }
	inline vector<vector<long> >    const& get_immediate_candidates_lba() { return immediate_candidates_logical_addresses; }
	void find_alternative_immediate_candidate(uint package, uint die);

	//inline vector<vector<long> > const& get_immediate_candidates_logical_addresses() { return immediate_candidates_logical_addresses; }
private:
	void set_new_candidate();
	struct progress_tracker {
		vector<Address_Range> ranges;
		uint current_range;
		uint offset_in_range;
		vector<vector<bool> > completion_bitmap;
		inline bool finished() const { return current_range >= ranges.size(); }
		inline long get_next_lba();
		progress_tracker(vector<Address_Range> ranges);
	};
	progress_tracker pt;

	struct candidate {
		long logical_address;
		Address physical_address;
		candidate(Address phys, long log) : logical_address(log), physical_address(phys) {}
	};
	vector<vector<vector<candidate> > > candidate_list;	// current physcial addresses, ready to be read.

	vector<vector<Address> > immediate_candidates_physical_addresses;
	vector<vector<long> > immediate_candidates_logical_addresses;

	uint finished_counter;
	FtlParent const& ftl;
};

class Flexible_Read_Event : public Event {
private:
	Flexible_Reader* reader;
public:
	Flexible_Read_Event(Flexible_Reader* fr, double time) : Event(READ, 0, 1, time), reader(fr) {};
	~Flexible_Read_Event() {}
	inline vector<vector<Address> > const& get_candidates()     { return reader->get_immediate_candidates(); }
	inline vector<vector<long> >    const& get_candidates_lba() { return reader->get_immediate_candidates_lba(); }
	inline void register_read_commencement() { reader->register_read_commencement(this); }
	inline void find_alternative_immediate_candidate(uint package, uint die) { reader->find_alternative_immediate_candidate(package, die); }
};

class Flexible_Reader_Thread : public Thread
{
public:
	Flexible_Reader_Thread(long min_LBA, long max_LAB, int number_of_times_to_repeat);
	void issue_first_IOs();
	void handle_event_completion(Event* event);
private:
	long min_LBA, max_LBA;
	bool ready_to_issue_next_write;
	int number_of_times_to_repeat;
	Flexible_Reader* flex_reader;
};

// This class can be extended to allow customized IO scheduling policies
class OS_Scheduler {
public:
	virtual ~OS_Scheduler() {}
	virtual int pick(unordered_map<int, Thread*> const& threads) = 0;
};

// This is a FIFO scheduler that implements a simple IO queue.
class FIFO_OS_Scheduler : public OS_Scheduler {
public:
	int pick(unordered_map<int, Thread*> const& threads);
};

// This is a fair IO scheduler that tries to schedule IOs from different threads in round robin
class FAIR_OS_Scheduler : public OS_Scheduler {
public:
	FAIR_OS_Scheduler() : last_id(0) {}
	int pick(unordered_map<int, Thread*> const& threads);
private:
	int last_id;
};

class OperatingSystem
{
public:
	OperatingSystem();
	void set_threads(vector<Thread*> threads);
	vector<Thread*> get_non_finished_threads();
	void init_threads();
	~OperatingSystem();
	void run();
	void check_if_stuck(bool no_pending_event, bool queue_is_full);
	void print_progess();
	void register_event_completion(Event* event);
	void set_num_writes_to_stop_after(long num_writes);
	void set_progress_meter_granularity(int num) { progress_meter_granularity = num; }
	Flexible_Reader* create_flexible_reader(vector<Address_Range>);
	void submit(Event* event);
	Ssd* get_ssd() { return ssd; }
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
    	ar & ssd;
    }
private:
	void dispatch_event(int thread_id);
	double get_event_minimal_completion_time(Event const*const event) const;
	void setup_follow_up_threads(int thread_id, double time);
	Ssd * ssd;
	unordered_map<int, Thread*> threads;
	vector<Thread*> historical_threads;
	unordered_map<long, long> app_id_to_thread_id_mapping;
	set<uint> currently_executing_ios;
	long NUM_WRITES_TO_STOP_AFTER;
	long num_writes_completed;
	int counter_for_user;
	int idle_time;
	double time;
	static int thread_id_generator;
	OS_Scheduler* scheduler;
	int progress_meter_granularity;
};

}

#endif /* OPERATING_SYSTEM_H_ */
