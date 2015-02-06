
/* ssd.h
 * Main SSD header file
 * 	Lists definitions of all classes, structures,
 * 		typedefs, and constants used in ssd namespace
 *		Controls options, such as debug asserts and test code insertions
 */

#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <stack>
#include <queue>
#include <deque>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <algorithm>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/deque.hpp>
#include <boost/serialization/utility.hpp>
#include <boost/serialization/base_object.hpp>
#include <sstream>
#include <initializer_list>
#include <boost/archive/xml_iarchive.hpp>
#include <boost/archive/xml_oarchive.hpp>

#include <iostream>

#include <boost/serialization/export.hpp>



/*#include <boost/multi_index_container.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/random_access<_index.hpp>*/
#include "bloom_filter.hpp"
#include <sys/types.h>
//#include "mtrand.h" // Marsenne Twister random number generator
#include <fstream>
#include "block_management.h"
#include "scheduler.h"
#include "Operating_System.h"

#ifndef _SSD_H
#define _SSD_H

using namespace std;

namespace ssd {

/* define exit codes for errors */
#define MEM_ERR -1
#define FILE_ERR -2

/* Uncomment to disable asserts for production */
#define NDEBUG

/* Simulator configuration from ssd_config.cpp */

/* Configuration file parsing for extern config variables defined below */
void load_entry(char *name, double value, uint line_number);
void load_config(const char * const file_name);
void set_big_SSD_config();
void set_small_SSD_config();
void print_config(FILE *stream);

/* Ram class:
 * 	delay to read from and write to the RAM for 1 page of data */
extern const double RAM_READ_DELAY;
extern const double RAM_WRITE_DELAY;

extern int OS_SCHEDULER;

/* Bus class:
 * 	delay to communicate over bus
 * 	max number of connected devices allowed
 * 	flag value to detect free table entry (keep this negative)
 * 	number of time entries bus has to keep track of future schedule usage
 * 	number of simultaneous communication channels - defined by SSD_SIZE */
extern double BUS_CTRL_DELAY;
extern double BUS_DATA_DELAY;
extern const uint BUS_MAX_CONNECT;
extern const double BUS_CHANNEL_FREE_FLAG;
extern const uint BUS_TABLE_SIZE;
/* extern const uint BUS_CHANNELS = 4; same as # of Packages, defined by SSD_SIZE */

/* Ssd class:
 * 	number of Packages per Ssd (size) */
extern uint SSD_SIZE;

/* Package class:
 * 	number of Dies per Package (size) */
extern uint PACKAGE_SIZE;

/* Die class:
 * 	number of Planes per Die (size) */
extern uint DIE_SIZE;

/* Plane class:
 * 	number of Blocks per Plane (size)
 * 	delay for reading from plane register
 * 	delay for writing to plane register
 * 	delay for merging is based on read, write, reg_read, reg_write 
 * 		and does not need to be explicitly defined */
extern uint PLANE_SIZE;
extern const double PLANE_REG_READ_DELAY;
extern const double PLANE_REG_WRITE_DELAY;

/* Block class:
 * 	number of Pages per Block (size)
 * 	number of erases in lifetime of block
 * 	delay for erasing block */
extern uint BLOCK_SIZE;
extern const uint BLOCK_ERASES;
extern double BLOCK_ERASE_DELAY;

/* Page class:
 * 	delay for Page reads
 * 	delay for Page writes */
extern double PAGE_READ_DELAY;
extern double PAGE_WRITE_DELAY;
extern const uint PAGE_SIZE;
extern const bool PAGE_ENABLE_DATA;

// a 0-1 factor indicating the percentage of the logical address space out of the physical address space
extern double OVER_PROVISIONING_FACTOR;
/*
 * Mapping directory
 */
extern const uint MAP_DIRECTORY_SIZE;

extern bool ALLOW_DEFERRING_TRANSFERS;

/*
 * FTL Implementation
 */
extern const uint FTL_IMPLEMENTATION;

/* Virtual block size (as a multiple of the physical block size) */
//extern const uint VIRTUAL_BLOCK_SIZE;

/* Virtual page size (as a multiple of the physical page size) */
//extern const uint VIRTUAL_PAGE_SIZE;

// extern const uint NUMBER_OF_ADDRESSABLE_BLOCKS;
static inline uint NUMBER_OF_ADDRESSABLE_BLOCKS() {
	return SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE;
}

static inline uint NUMBER_OF_ADDRESSABLE_PAGES() {
	return SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE;
}

/*
 * Memory area to support pages with data.
 */
extern void *page_data;
extern void *global_buffer;

/*
 * Controls the block manager to be used
 */
extern int BLOCK_MANAGER_ID;
extern int GARBAGE_COLLECTION_POLICY;
extern int GREED_SCALE;
extern int SEQUENTIAL_LOCALITY_THRESHOLD;
extern bool ENABLE_TAGGING;
extern int WRITE_DEADLINE;
extern int READ_DEADLINE;
extern int READ_TRANSFER_DEADLINE;

extern int FTL_DESIGN;
extern bool IS_FTL_PAGE_MAPPING;

extern int SRAM;

/*
 * Controls the level of detail of output
 */
extern int PRINT_LEVEL;
extern bool PRINT_FILE_MANAGER_INFO;

/* Defines the max number of copy back operations on a page before ECC check is performed.
 * Set to zero to disable copy back GC operations */
extern uint MAX_REPEATED_COPY_BACKS_ALLOWED;

/* Defines the max number of page addresses in map keeping track of each pages copy back count */
extern uint MAX_ITEMS_IN_COPY_BACK_MAP;

/* Defines the maximal length of the SSD queue  */
extern int MAX_SSD_QUEUE_SIZE;

/* Defines how the sequential writes detection algorithm spreads a sequential write  */
extern uint LOCALITY_PARALLEL_DEGREE;

extern bool USE_ERASE_QUEUE;

extern int SCHEDULING_SCHEME;
extern bool BALANCEING_SCHEME;

extern bool ENABLE_WEAR_LEVELING;
extern int WEAR_LEVEL_THRESHOLD;
extern int MAX_ONGOING_WL_OPS;
extern int MAX_CONCURRENT_GC_OPS;

extern int PAGE_HOTNESS_MEASURER;

/* Enumerations to clarify status integers in simulation
 * Do not use typedefs on enums for reader clarity */

/* Page states
 * 	empty   - page ready for writing (and contains no valid data)
 * 	valid   - page has been written to and contains valid data
 * 	invalid - page has been written to and does not contain valid data */
enum page_state{EMPTY, VALID, INVALID};

/* Block states
 * 	free     - all pages in block are empty
 * 	active   - some pages in block are valid, others are empty or invalid
 * 	inactive - all pages in block are invalid */
enum block_state{FREE, PARTIALLY_FREE, ACTIVE, INACTIVE};

/* I/O request event types
 * 	read  - read data from address. Performs both read_command and read_transfer. Kept here for legacy purposes
 * 	read_command - the first part of a read with the command and actual read on the chip
 * 	read_transfer - transfer read data back to the controller
 * 	write - write data to address (page state set to valid)
 * 	erase - erase block at address (all pages in block are erased - 
 * 	                                page states set to empty)
 * 	merge - move valid pages from block at address (page state set to invalid)
 * 	           to free pages in block at merge_address */
enum event_type{NOT_VALID, READ, READ_COMMAND, READ_TRANSFER, WRITE, ERASE, MERGE, TRIM, GARBAGE_COLLECTION, COPY_BACK, MESSAGE};

/* General return status
 * return status for simulator operations that only need to provide general
 * failure notifications */
enum status{FAILURE, SUCCESS};

/* Address valid status
 * used for the valid field in the address class
 * example: if valid == BLOCK, then
 * 	the package, die, plane, and block fields are valid
 * 	the page field is not valid */
enum address_valid{NONE, PACKAGE, DIE, PLANE, BLOCK, PAGE};

/*
 * Enumeration of page access patterns
 */
enum write_hotness {WRITE_HOT, WRITE_COLD};
enum read_hotness {READ_HOT, READ_COLD};

#define BOOST_MULTI_INDEX_ENABLE_SAFE_MODE 1

enum age {YOUNG, OLD};

/* List classes up front for classes that have references to their "parent"
 * (e.g. a Package's parent is a Ssd).
 *
 * The order of definition below follows the order of this list to support
 * cases of agregation where the agregate class should be defined first.
 * Defining the agregate class first enables use of its non-default
 * constructors that accept args
 * (e.g. a Ssd contains a Controller, Ram, Bus, and Packages). */
class Address;
class Stats;
class Event;
class Flexible_Read_Event;
class Page;
class Block;
class Plane;
class Die;
class Package;

class FtlParent;
class FtlImpl_Page;
class DFTL;
class FAST;
class Ssd;

class event_queue;
class IOScheduler;
class Scheduling_Strategy;

class Block_manager_parent;
class Block_manager_parallel;
class Shortest_Queue_Hot_Cold_BM;
class Wearwolf;
class Sequential_Locality_BM;
class Wear_Leveling_Strategy;
class Garbage_Collector;
class flash_resident_ftl_garbage_collection;
class Migrator;

class Sequential_Pattern_Detector;
class Page_Hotness_Measurer;
class Random_Order_Iterator;

class OperatingSystem;
class Thread;
class Synchronous_Writer;

struct Address_Range;
class Flexible_Reader;

class MTRand_int32;


/* Class to manage physical addresses for the SSD.  It was designed to have
 * public members like a struct for quick access but also have checking,
 * printing, and assignment functionality.  An instance is created for each
 * physical address in the Event class. */
class Address
{
public:
	uint package;
	uint die;
	uint plane;
	uint block;
	uint page;
	enum address_valid valid;
	Address();
	inline Address(const Address &address) { *this = address; }
	inline Address(const Address *address) { *this = *address; }
	Address(uint package, uint die, uint plane, uint block, uint page, enum address_valid valid);
	Address(uint address, enum address_valid valid);
	~Address() {}
	enum address_valid compare(const Address &address) const;
	void print(FILE *stream = stdout) const;
	void set_linear_address(ulong address, enum address_valid valid);
	void set_linear_address(ulong address);
	ulong get_linear_address() const;
	long get_block_id() const { return (get_linear_address() - page) / BLOCK_SIZE; }
	inline Address& operator=(const Address &rhs)
	{
		if(this == &rhs)
			return *this;
		package = rhs.package;
		die = rhs.die;
		plane = rhs.plane;
		block = rhs.block;
		page = rhs.page;
		valid = rhs.valid;
		return *this;
	}

	bool operator<(const Address &rhs) const {
		return this->get_linear_address() < rhs.get_linear_address();
	}

    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
    	ar & package;
    	ar & die;
    	ar & plane;
    	ar & block;
    	ar & page;
    	ar & valid;
    }
};

/* Class to emulate a log block with page-level mapping. */
/*class LogPageBlock
{
public:
	LogPageBlock();
	~LogPageBlock();

	int *pages;
	long *aPages;
	Address address;
	int numPages;

	LogPageBlock *next;

	bool operator() (const ssd::LogPageBlock& lhs, const ssd::LogPageBlock& rhs) const;
	bool operator() (const ssd::LogPageBlock*& lhs, const ssd::LogPageBlock*& rhs) const;
};*/


/* Class to manage I/O requests as events for the SSD.  It was designed to keep
 * track of an I/O request by storing its type, addressing, and timing.  The
 * SSD class creates an instance for each I/O request it receives. */
class Event 
{
public:
	Event(enum event_type type, ulong logical_address, uint size, double start_time);
	Event();
	Event(Event const& event);
	inline virtual ~Event() {}
	inline ulong get_logical_address() const 			{ return logical_address; }
	inline void set_logical_address(ulong addr) 		{ logical_address = addr; }
	inline const Address &get_address() const 			{ return address; }
	inline const Address &get_replace_address() const 	{ return replace_address; }
	inline uint get_size() const 						{ return size; }
	inline void set_size(int new_size)  				{ size = new_size; }
	inline enum event_type get_event_type() const 		{ return type; }
	inline double get_start_time() const 				{ assert(start_time >= 0.0); return start_time; }
	inline bool is_original_application_io() const 		{ return original_application_io; }
	inline void set_original_application_io(bool val) 	{ original_application_io = val; }
	inline double get_execution_time() const 			{ assert(execution_time >= 0.0); return execution_time; }
	inline double get_accumulated_wait_time() const 	{ assert(accumulated_wait_time >= 0.0); return accumulated_wait_time; }
	inline double get_current_time() const 				{ return start_time + os_wait_time + accumulated_wait_time + bus_wait_time + execution_time; }
	inline double get_ssd_submission_time() const 		{ return start_time + os_wait_time; }
	inline uint get_application_io_id() const 			{ return application_io_id; }
	inline double get_bus_wait_time() const 			{ assert(bus_wait_time >= 0.0); return bus_wait_time; }
	inline double get_os_wait_time() const 				{ return os_wait_time; }
	inline bool get_noop() const 						{ return noop; }
	inline uint get_id() const 							{ return id; }
	inline int get_tag() const 							{ return tag; }
	inline void set_tag(int new_tag) 					{ tag = new_tag; }
	inline void set_thread_id(int new_thread_id)		{ thread_id = new_thread_id; }
	inline void set_address(const Address &address) {
		if (type == WRITE || type == READ || type == READ_COMMAND || type == READ_TRANSFER)
			assert(address.valid == PAGE);
		this -> address = address;
	}
	inline void set_start_time(double time) 				{ start_time = time; }
	inline void set_replace_address(const Address &address) { replace_address = address; }
	inline void set_payload(void *payload) 					{ this->payload = payload; }
	inline void set_event_type(const enum event_type &type) { this->type = type; }
	inline void set_noop(bool value) 						{ noop = value; }
	inline void set_application_io_id(uint value)			{ application_io_id = value; }
	inline void set_garbage_collection_op(bool value) 		{ garbage_collection_op = value; }
	inline void set_mapping_op(bool value) 					{ mapping_op = value; }
	inline void set_age_class(int value) 					{ age_class = value; }
	inline void set_copyback(bool value)					{ copyback = value; }
	inline void set_cached_write(bool value)				{ cached_write = value; }
	inline bool is_cached_write()							{ return cached_write; }
	inline int get_age_class() const 						{ return age_class; }
	inline bool is_garbage_collection_op() const 			{ return garbage_collection_op; }
	inline bool is_mapping_op() const 						{ return mapping_op; }
	inline void *get_payload() const 						{ return payload; }
	inline bool is_copyback() const 						{ return copyback; }
	inline void incr_bus_wait_time(double time_incr) 		{ assert(time_incr >= 0); bus_wait_time += time_incr; incr_pure_ssd_wait_time(time_incr); }
	inline void incr_pure_ssd_wait_time(double time_incr) 	{ pure_ssd_wait_time += time_incr;}
	inline void incr_os_wait_time(double time_incr) 		{ os_wait_time += time_incr; }
	inline void incr_execution_time(double time_incr) 		{ execution_time += time_incr; incr_pure_ssd_wait_time(time_incr);  }
	inline void incr_accumulated_wait_time(double time_incr) 	{ accumulated_wait_time += time_incr;  }
	inline double get_overall_wait_time() const 				{ return accumulated_wait_time + bus_wait_time; }
	inline double get_latency() const 				{ return pure_ssd_wait_time; }
	inline bool is_wear_leveling_op() const { return wear_leveling_op ; }
	inline void set_wear_leveling_op(bool value) { wear_leveling_op = value; }
	void print(FILE *stream = stdout) const;
	static void reset_id_generators();
	bool is_flexible_read();
	inline void increment_iteration_count() { num_iterations_in_scheduler++; }
	inline int get_iteration_count() { return num_iterations_in_scheduler; }
	inline int get_ssd_id() { return ssd_id; }
	inline void set_ssd_id(int new_ssd_id) { ssd_id = new_ssd_id; }
protected:
	long double start_time;
	double execution_time;
	double bus_wait_time;
	double os_wait_time;
	enum event_type type;
	double accumulated_wait_time;

	ulong logical_address;
	Address address;
	Address replace_address;
	uint size;
	void *payload;
	bool noop;

	bool garbage_collection_op;
	bool wear_leveling_op;
	bool mapping_op;
	bool original_application_io;
	bool copyback;
	bool cached_write;

	// an ID for a single IO to the chip. This is not actually used for any logical purpose
	static uint id_generator;
	uint id;

	// an ID to manage dependencies in the scheduler.
	uint application_io_id;
	static uint application_io_id_generator;

	uint ssd_id;

	int age_class;
	int tag;

	int thread_id;
	double pure_ssd_wait_time;
	int num_iterations_in_scheduler;
};

class Message : public Event {
public:
	Message(double time) : Event(MESSAGE, 0, 1, time) {}
};



/* The page is the lowest level data storage unit that is the size unit of
 * requests (events).  Pages maintain their state as events modify them. */
class Page 
{
public:
	inline Page() : state(EMPTY), logical_addr(-1) {}
	inline ~Page() {}
	enum status _read(Event &event);
	enum status _write(Event &event);
	inline enum page_state get_state() const { return state; }
	inline void set_state(page_state val) { state = val; }
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & state;
    }
    void set_logical_addr(int l) { logical_addr = l;}
    int get_logical_addr() const { return logical_addr; }
private:
	enum page_state state;
	int logical_addr;
};

/* The block is the data storage hardware unit where erases are implemented.
 * Blocks maintain wear statistics for the FTL. */
class Block 
{
public:
	Block(long physical_address);
	Block();
	~Block() {}
	enum status read(Event &event);
	enum status write(Event &event);
	enum status _erase(Event &event);
	inline uint get_pages_valid() const { return pages_valid; }
	inline uint get_pages_invalid() const { return pages_invalid; }
	inline enum block_state get_state() const {
		return 	pages_invalid == BLOCK_SIZE ? INACTIVE :
				pages_valid == BLOCK_SIZE ? ACTIVE :
				pages_invalid + pages_valid == BLOCK_SIZE ? ACTIVE : PARTIALLY_FREE;
	}
	inline ulong get_erases_remaining() const { return erases_remaining; }
	void invalidate_page(uint page);
	inline long get_physical_address() const { return physical_address; }
	inline Block *get_pointer() { return this; }
	inline Page const& get_page(int i) const { return data[i]; }
	inline ulong get_age() const { return BLOCK_ERASES - erases_remaining; }
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
    	ar & pages_invalid;
    	ar & physical_address;
    	ar & data;
    	ar & pages_valid;
    	ar & erases_remaining;
    }
private:
	uint pages_invalid;
	long physical_address;
	vector<Page> data;
	uint pages_valid;
	ulong erases_remaining;
};

/* The plane is the data storage hardware unit that contains blocks.*/
class Plane 
{
public:
	Plane(long physical_address);
	Plane();
	~Plane() {}
	enum status read(Event &event);
	enum status write(Event &event);
	enum status erase(Event &event);
	inline Block *get_block(int i) { return &data[i]; }
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & data;
    }
private:
	vector<Block> data;
};

/* The die is the data storage hardware unit that contains planes and is a flash
 * chip.  Dies maintain wear statistics for the FTL. */
class Die 
{
public:
	Die(long physical_address);
	Die();
	~Die() {}
	enum status read(Event &event);
	enum status write(Event &event);
	enum status erase(Event &event);
	double get_currently_executing_io_finish_time();
	inline Plane *get_plane(int i) { return &data[i]; }
	void clear_register();
	int get_last_read_application_io();
	bool register_is_busy();
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
    	ar & data;
    }
private:
	vector<Plane> data;
	double currently_executing_io_finish_time;
	int last_read_io;
};

/* The package is the highest level data storage hardware unit.  While the
 * package is a virtual component, events are passed through the package for
 * organizational reasons, including helping to simplify maintaining wear
 * statistics for the FTL. */
class Package 
{
public:
	Package (long physical_address);
	Package();
	~Package () {}
	enum status read(Event &event);
	enum status write(Event &event);
	enum status erase(Event &event);
	inline Die *get_die(int i) { return &data[i]; }
	enum status lock(double start_time, double duration, Event &event);
	inline double get_currently_executing_operation_finish_time() { return currently_executing_operation_finish_time; }
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
    	ar & data;
    }
private:
	vector<Die> data;
	double currently_executing_operation_finish_time;
};

extern const int UNDEFINED;
extern const int INFINITE;

class Page_Hotness_Measurer {
public:
//	virtual Page_Hotness_Measurer() = 0;
	virtual ~Page_Hotness_Measurer() {};
	virtual void register_event(Event const& event) = 0; // Inform hotness measurer about a read or write event
	virtual enum write_hotness get_write_hotness(unsigned long page_address) const = 0; // Return write hotness of a given page address
	virtual enum read_hotness get_read_hotness(unsigned long page_address) const = 0; // Return read hotness of a given page address
	virtual Address get_best_target_die_for_WC(enum read_hotness rh) const = 0; // Return address of die with leads WC data (with chosen read hotness)
};

class Ignorant_Hotness_Measurer : public Page_Hotness_Measurer {
public:
//	virtual Page_Hotness_Measurer() = 0;
	Ignorant_Hotness_Measurer() {};
	~Ignorant_Hotness_Measurer() {};
	void register_event(Event const& event) {};
	enum write_hotness get_write_hotness(unsigned long page_address) const { return WRITE_HOT; }
	enum read_hotness get_read_hotness(unsigned long page_address) const { return READ_HOT; }
	Address get_best_target_die_for_WC(enum read_hotness rh) const { return Address(); }
};

// Simple (naïve page hotness measurer implementation)
class Simple_Page_Hotness_Measurer : public Page_Hotness_Measurer {
public:
	Simple_Page_Hotness_Measurer();
	~Simple_Page_Hotness_Measurer();
	void register_event(Event const& event);
	enum write_hotness get_write_hotness(unsigned long page_address) const;
	enum read_hotness get_read_hotness(unsigned long page_address) const;
	Address get_best_target_die_for_WC(enum read_hotness rh) const;
private:
	void start_new_interval_writes();
	void start_new_interval_reads();
	map<ulong, uint> write_current_count;
	vector<double> write_moving_average;
	map<ulong, uint> read_current_count;
	vector<double> read_moving_average;
	uint current_interval;
	double average_write_hotness;
	double average_read_hotness;
	vector<vector<uint> > writes_per_die;
	vector<vector<uint> > reads_per_die;
	vector<vector<double> > average_reads_per_die;
	vector<vector<uint> > current_reads_per_die;
	uint writes_counter;
	uint reads_counter;
	const uint WINDOW_LENGTH;
	const uint KICK_START;
};


// BloomFilter hotness
typedef vector< bloom_filter > hot_bloom_filter;
typedef vector< vector<uint> > lun_counters;


class Die_Stats {
public:
	Die_Stats(bloom_parameters bloomfilter_parameters, uint read_window_size, uint write_window_size)
	:	live_pages(0),
	 	reads(0),
	 	reads_targeting_wc_pages(0),
	 	reads_targeting_wc_pages_previous_window(UNDEFINED),
	 	writes(0),
	 	unique_wh_encountered(0),
	 	unique_wh_encountered_previous_window(UNDEFINED),
	 	wh_counted_already(bloomfilter_parameters),
	 	read_counter_window_size(read_window_size),
	 	write_counter_window_size(write_window_size)
	{}

	Die_Stats(const Die_Stats& object)
	:	live_pages(object.live_pages),
	 	reads(object.reads),
	 	reads_targeting_wc_pages(object.reads_targeting_wc_pages),
	 	reads_targeting_wc_pages_previous_window(object.reads_targeting_wc_pages_previous_window),
	 	writes(object.writes),
	 	unique_wh_encountered(object.unique_wh_encountered),
	 	unique_wh_encountered_previous_window(object.unique_wh_encountered_previous_window),
	 	read_counter_window_size(object.read_counter_window_size),
	 	write_counter_window_size(object.write_counter_window_size)
	{
		wh_counted_already = object.wh_counted_already;
	}

	// WC = live pages - WH
	inline uint get_wc_pages() const {
		int unique_wh = unique_wh_encountered_previous_window == UNDEFINED ? unique_wh_encountered : unique_wh_encountered;
		int diff = live_pages - unique_wh;
		assert(diff >= 0);
		return diff;
	}

	inline uint get_reads_targeting_wc_pages() const {
		if (reads_targeting_wc_pages_previous_window != UNDEFINED) return reads_targeting_wc_pages_previous_window;
		if (reads == 0) return 0;
		return (uint) reads_targeting_wc_pages * ((double) read_counter_window_size / reads); // Compensate for a partially completed 1st window
	}

	void print() const {
		printf("live |\tr\tr->wc\tr>wc(l)\tw\tuq_wh\tuq_wh(l)\n");
		printf("%u\t%u\t%u\t%d\t%u\t%d\t%d\t\n", live_pages, reads, reads_targeting_wc_pages, reads_targeting_wc_pages_previous_window,
				writes, unique_wh_encountered, unique_wh_encountered_previous_window);
	}
	uint live_pages;

	uint reads;
	uint reads_targeting_wc_pages;
	int reads_targeting_wc_pages_previous_window;

	uint writes;
	int unique_wh_encountered;
	int unique_wh_encountered_previous_window;
	bloom_filter wh_counted_already;

	uint read_counter_window_size;
	uint write_counter_window_size;
};

class BloomFilter_Page_Hotness_Measurer : public Page_Hotness_Measurer {
friend class Die_Stats;
public:
	BloomFilter_Page_Hotness_Measurer(uint num_bloom_filters = 4, uint bloom_filter_size = 2048, uint IOs_before_decay = 512, bool preheat = true);
	~BloomFilter_Page_Hotness_Measurer();
	void register_event(Event const& event);
	enum write_hotness get_write_hotness(unsigned long page_address) const;
	enum read_hotness get_read_hotness(unsigned long page_address) const;
	Address get_best_target_die_for_WC(enum read_hotness rh) const;
	void heat_all_addresses();

	// Debug output
	void print_die_stats() const;

private:
	double get_hot_data_index(event_type type, unsigned long page_address) const;
	inline double get_max_hot_data_index_value() { return num_bloom_filters + 1; } // == (2 / (double) V) * (V + 1) * (V / 2)

	// Parameters
	uint num_bloom_filters, bloom_filter_size, IOs_before_decay;
	uint hotness_threshold, read_counter_window_size, write_counter_window_size;

	// Bookkeeping variables
	uint read_oldest_BF, write_oldest_BF;
	uint read_counter, write_counter;
	hot_bloom_filter read_bloom;
	hot_bloom_filter write_bloom;
	vector< vector<Die_Stats> > package_die_stats;

	//	vector< map<int, bool> > ReadErrorFreeCounter;
//	vector< map<int, bool> > WriteErrorFreeCounter;

};

class Random_Order_Iterator {
public:
	static vector<int> get_iterator(int needed_length);
private:
	Random_Order_Iterator() {}
	static void shuffle(vector<int>&);
	static MTRand_int32 random_number_generator;
};

class FtlParent
{
public:
	FtlParent(Ssd *ssd, Block_manager_parent* bm);
	FtlParent() : ssd(NULL), scheduler(NULL), bm(NULL), normal_stats("normal_stats", 50000) {};
	virtual void set_scheduler(IOScheduler* sched) { scheduler = sched; }
	virtual ~FtlParent ();
	virtual void read(Event *event) = 0;
	virtual void write(Event *event) = 0;
	virtual void trim(Event *event) = 0;
	virtual void register_write_completion(Event const& event, enum status result) = 0;
	virtual void register_read_completion(Event const& event, enum status result) = 0;
	virtual void register_trim_completion(Event & event) = 0;
	virtual long get_logical_address(uint physical_address) const = 0;
	virtual Address get_physical_address(uint logical_address) const = 0;
	virtual void set_replace_address(Event& event) const = 0;
	virtual void set_read_address(Event& event) const = 0;
	virtual void register_erase_completion(Event & event) {};
	virtual void print() const {};

	void set_block_manager(Block_manager_parent* b) { bm = b; }
	Block_manager_parent* get_block_manager() { return bm; }
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
    	ar & ssd;
    	ar & scheduler;
    	ar & bm;
    }
protected:
	Ssd *ssd;
	IOScheduler *scheduler;
	Block_manager_parent* bm;
	void collect_stats(Event const& event);
	struct stats {
		stats(string name, long counter_limit);
		string file_name;
		long num_noop_reads_per_interval;
		long num_noop_writes_per_interval;
		long num_mapping_writes;
		long num_mapping_reads;
		long mapping_reads_per_interval;
		long mapping_writes_per_interval;
		long gc_reads_per_interval;
		long gc_writes_per_interval;
		long gc_mapping_writes_per_interval;
		long gc_mapping_reads_per_interval;
		long app_reads_per_interval;
		long app_writes_per_interval;
		long num_noop_reads;
		long num_noop_writes;

		long counter;
		const long COUNTER_LIMIT;
		void print() const;
		void collect_stats(Event const& event);
	};
	stats normal_stats;
};

class FtlImpl_Page : public FtlParent
{
public:
	FtlImpl_Page(Ssd *ssd, Block_manager_parent* bm);
	FtlImpl_Page();
	~FtlImpl_Page();
	void read(Event *event);
	void write(Event *event);
	void trim(Event *event);
	void register_write_completion(Event const& event, enum status result);
	void register_read_completion(Event const& event, enum status result);
	void register_trim_completion(Event & event);
	long get_logical_address(uint physical_address) const;
	Address get_physical_address(uint logical_address) const;
	void set_replace_address(Event& event) const;
	void set_read_address(Event& event) const;
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
    	ar & boost::serialization::base_object<FtlParent>(*this);
    	ar & logical_to_physical_map;
    	ar & physical_to_logical_map;
    }
private:
	vector<long> logical_to_physical_map;
	vector<long> physical_to_logical_map;
};



class ftl_cache {
public:
	void register_write_arrival(Event const&  app_write);
	bool register_read_arrival(Event* app_read);
	void register_write_completion(Event const& app_write);
	void handle_read_dependency(Event* event);
	void clear_clean_entries(double time);
	int choose_dirty_victim(double time);
	int get_num_dirty_entries() const;
	bool mark_clean(int key, double time);
	int erase_victim(double time, bool allow_flushing_dirty);
	bool contains(int key) const;
	void set_synchronized(int key);
	static int CACHED_ENTRIES_THRESHOLD;

	struct entry {
		entry() : dirty(false), synch_flag(false), fixed(false), hotness(0), timestamp(numeric_limits<double>::infinity()) {}
		bool dirty;
		bool synch_flag;
		int fixed;
		short hotness;
		double timestamp; // when was the entry added to the cache
	};
	unordered_map<long, entry> cached_mapping_table; // maps logical addresses to physical addresses
	queue<long> eviction_queue_dirty;
	queue<long> eviction_queue_clean;
private:
	void iterate(long& victim_key, entry& victim_entry, bool allow_choosing_dirty);
};

class flash_resident_page_ftl : public FtlParent {
public:
	flash_resident_page_ftl(Ssd *ssd, Block_manager_parent* bm) :
		FtlParent(ssd, bm), cache(new ftl_cache()), page_mapping(new FtlImpl_Page(ssd, bm)), gc(NULL) {}
	flash_resident_page_ftl() : FtlParent(), gc(NULL), cache(new ftl_cache()), page_mapping(NULL) {}
	ftl_cache* get_cache() { return cache; }
	void set_gc(flash_resident_ftl_garbage_collection* new_gc) { gc = new_gc; }
	FtlImpl_Page* get_page_mapping() { return page_mapping; }
	void update_bitmap(vector<bool>& bitmap, Address block_addr);
	void set_synchronized(int logical_address);
protected:
	ftl_cache* cache;
	FtlImpl_Page* page_mapping;
	flash_resident_ftl_garbage_collection* gc;
};


class DFTL : public flash_resident_page_ftl {
public:
	DFTL(Ssd *ssd, Block_manager_parent* bm);
	DFTL();
	~DFTL();
	void read(Event *event);
	void write(Event *event);
	void trim(Event *event);
	void register_write_completion(Event const& event, enum status result);
	void register_read_completion(Event const& event, enum status result);
	void register_trim_completion(Event & event);
	long get_logical_address(uint physical_address) const;
	Address get_physical_address(uint logical_address) const;
	void set_replace_address(Event& event) const;
	void set_read_address(Event& event) const;
	void print() const;
	void print_short() const;
	static int ENTRIES_PER_TRANSLATION_PAGE;
	static bool SEPERATE_MAPPING_PAGES;

private:
	void notify_garbage_collector(int translation_page_id, double time);
	//bool flush_mapping(double time, bool allow_flushing_dirty);
	//void iterate(long& victim_key, ftl_cache::entry& victim_entry, bool allow_choosing_dirty);
	void create_mapping_read(long translation_page_id, double time, Event* dependant);
	void mark_clean(long translation_page_id, Event const& event);
	void try_clear_space_in_mapping_cache(double time);
	set<long> ongoing_mapping_operations; // contains the logical addresses of ongoing mapping IOs
	unordered_map<long, vector<Event*> > application_ios_waiting_for_translation; // maps translation page ids to application IOs awaiting translation
	struct mapping_page {
		map<int, Address> entries;
	};
	vector<mapping_page> mapping_pages;
	struct dftl_statistics {
		map<int, int> cleans_histogram;
		map<int, int> address_hits;
	};
	dftl_statistics dftl_stats;
};


class FAST : public FtlParent {
public:
	FAST(Ssd *ssd, Block_manager_parent* bm, Migrator* migrator);
	FAST();
	~FAST();
	void read(Event *event);
	void write(Event *event);
	void trim(Event *event);
	void register_write_completion(Event const& event, enum status result);
	void register_read_completion(Event const& event, enum status result);
	void register_trim_completion(Event & event);
	long get_logical_address(uint physical_address) const;
	Address get_physical_address(uint logical_address) const;
	void set_replace_address(Event& event) const;
	void set_read_address(Event& event) const;
	void register_erase_completion(Event & event);
	void print() const;
    friend class boost::serialization::access;
    template<class Archive> void
    serialize(Archive & ar, const unsigned int version) {
    	ar & boost::serialization::base_object<FtlParent>(*this);
    	ar & translation_table;
    	ar & active_log_blocks_map;
    	ar & dial;
    	ar & NUM_LOG_BLOCKS;
    	ar & num_active_log_blocks;
    	ar & bm;
    	//ar & queued_events;
    	ar & migrator;
    	ar & page_mapping;
    	//ar & gc_queue;
    }
private:
	void schedule(Event* e);
	void choose_existing_log_block(Event* e);
	void unlock_block(Event const& event);
	void consider_doing_garbage_collection(double time);

	struct log_block {
		log_block(Address& addr) : addr(addr), num_blocks_mapped_inside() {}
		log_block() : addr(), num_blocks_mapped_inside() {}
		Address addr;
		set<int> num_blocks_mapped_inside;
	    friend class boost::serialization::access;
	    template<class Archive> void
	    serialize(Archive & ar, const unsigned int version) {
	    	ar & addr;
	    	ar & num_blocks_mapped_inside;
	    }
	};

	struct mycomparison
	{
	  bool operator() (const log_block* lhs, const log_block* rhs) const
	  {
	    return lhs->num_blocks_mapped_inside.size() > rhs->num_blocks_mapped_inside.size();
	  }
	};

	void write_in_log_block(Event* event);
	void queue_up(Event* e, Address const& lock);
	priority_queue<log_block*, std::vector<log_block*>, mycomparison> full_log_blocks;
	void release_events_there_was_no_space_for();
	void garbage_collect(int block_id, log_block* log_block, double time);

	vector<Address> translation_table;		  // maps block ID to a block address in flash. This is the main mapping table
	map<int, log_block*> active_log_blocks_map;  // Maps a block ID to the address of the corresponding log block. Used to quickly determine where to place an update
	int dial;
	int NUM_LOG_BLOCKS;
	int num_active_log_blocks;
	map<int, queue<Event*> > queued_events; // stores events tar
	Migrator* migrator;
	FtlImpl_Page page_mapping;
	map<int, queue<Event*> > gc_queue;
	map<long, queue<Event*> > logical_dependencies;  // a locking table with page granularity
};

/* The SSD is the single main object that will be created to simulate a real
 * SSD.  Creating a SSD causes all other objects in the SSD to be created.  The
 * event_arrive method is where events will arrive from DiskSim. */
class Ssd
{
public:
	Ssd ();
	~Ssd();
	void submit(Event* event);
	void progress_since_os_is_waiting();
	void register_event_completion(Event * event);
	inline Package* get_package(int i) { return &data[i]; }
	void set_operating_system(OperatingSystem* os);
	FtlParent* get_ftl() const;
	enum status issue(Event *event);
	double get_currently_executing_operation_finish_time(int package);
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
    	ar & data;
    	ar & ftl;
    	ar & os;
    	ar & scheduler;
    }
    IOScheduler* get_scheduler() { return scheduler; }
    void execute_all_remaining_events();
private:
    void submit_to_ftl(Event* event);
	Package &get_data();
	vector<Package> data;
	double last_io_submission_time;
	OperatingSystem* os;
	FtlParent *ftl;
	IOScheduler *scheduler;

	struct io_map {
		void resiger_large_event(Event* e);
		void register_completion(Event* e);
		bool is_part_of_large_event(Event* e);
		bool is_finished(int id) const;
		Event* get_original_event(int id);
	private:
		map<int, int> io_counter;
		map<int, Event*> event_map;
	};
	io_map large_events_map;

};

class RaidSsd
{
public:
	RaidSsd (uint ssd_size = SSD_SIZE);
	~RaidSsd();
	double event_arrive(enum event_type type, ulong logical_address, uint size, double start_time);
	double event_arrive(enum event_type type, ulong logical_address, uint size, double start_time, void *buffer);
	void *get_result_buffer();
	void print_statistics();
	void reset_statistics();
	void write_statistics(FILE *stream);
	void write_header(FILE *stream);

	void print_ftl_statistics();
private:
	uint size;
	Ssd *Ssds;

};

class VisualTracer
{
public:
	static void init();
	static void init(string folder);
	static void register_completed_event(Event& event);
	static void print_horizontally(int last_how_many_characters = UNDEFINED);
	static void print_horizontally_with_breaks(ulong cursor = 0);
	static void print_horizontally_with_breaks_last(long how_many_chars);
	static string get_as_string(ulong cursor, ulong max, int chars_per_line);
	static void print_vertically();
	static void write_file();
	static bool write_to_file;
private:
	static void trim_from_start(int num_characters_from_start);
	static void write(int package, int die, char symbol, int length);
	static void write_with_id(int package, int die, char symbol, int length, vector<vector<char> > symbols);
	static vector<vector<vector<char> > > trace;
	static string file_name;

	static long amount_written_to_file;
};

class StateVisualiser
{
public:
	static void print_page_status();
	static void print_block_ages();
	static void print_page_valid_histogram();
	static Ssd * ssd;
	static void init(Ssd * ssd);
};

class StatisticsFormatter
{
public:
	static string histogram_csv(map<double, uint> histogram);
	static string stacked_histogram_csv(vector<map<double, uint> > histograms, vector<string> names);

};

class SsdStatisticsExtractor //(TM)
{
public:
	static void init(Ssd * ssd);
	SsdStatisticsExtractor(Ssd & ssd);
	~SsdStatisticsExtractor();
	static class SsdStatisticsExtractor* get_instance();
	static string age_histogram_csv();
	static uint max_age_freq();
	static uint max_age();
	static inline double get_age_histogram_bin_size() { return age_histogram_bin_size; }

private:
	static SsdStatisticsExtractor *inst;
	Ssd & ssd;
	static const double age_histogram_bin_size;
};

class Number {
public:
	virtual string toString() = 0;
	virtual double toDouble() = 0;
	virtual int toInt() = 0;
};
class Integer : public Number {
public:
	Integer(int num) : value(num) {};
	int value;
	string toString() { return to_string(value); };
	double toDouble() { return value; }
	int toInt() { return value; }
};
class Double : public Number {
public:
	Double(double num) : value(num) {};
	double value;
	string toString() { return to_string(value); };
	double toDouble() { return value; }
	int toInt() { return value; }
};

class StatisticData {
public:
	~StatisticData();
	static void init();
	static void register_statistic(string name, std::initializer_list<Number*> list);
	static void register_field_names(string name, std::initializer_list<string> list);
	static double get_count(string name, int column);
	static double get_sum(string name, int column);
	static double get_average(string name, int column);
	static double get_weighted_avg_of_col2_in_terms_of_col1(string name, int col1, int col2);	// Useful for calculating average values over time. Col1 1 is typically time in this case.
	static double get_standard_deviation(string name, int column);
	static void clean(string name);
	static string to_csv(string name);
	static map<string, StatisticData> statistics;
private:
	vector<string> names;			// titles of columns
	vector<vector<Number*> > data;	// a table of data.
};

class StatisticsGatherer
{
public:
	static StatisticsGatherer *get_global_instance();
	static void init();

	StatisticsGatherer();
	~StatisticsGatherer();

	void register_completed_event(Event const& event);
	void register_scheduled_gc(Event const& gc);
	void register_executed_gc(Block const& victim);
	void register_events_queue_length(uint queue_size, double time);
	void print() const;
	void print_simple(FILE* file = stdout);
	void print_gc_info();
	void print_mapping_info();
	void print_csv();
	inline double get_wait_time_histogram_bin_size() { return wait_time_histogram_bin_size; }

	string totals_csv_header();
	vector<string> totals_vector_header();
	string totals_csv_line();
	string latency_csv();
	string age_histogram_csv();
	string wait_time_histogram_appIOs_csv();
	string wait_time_histogram_all_IOs_csv();
	string queue_length_csv();
	string app_and_gc_throughput_csv();
	uint max_age();
	uint max_age_freq();
	vector<double> max_waittimes();
	uint total_reads() const;
	uint total_writes() const;
	double get_reads_throughput() const;
	double get_writes_throughput() const;
	double get_total_throughput() const;

	long num_gc_cancelled_no_candidate;
	long num_gc_cancelled_not_enough_free_space;
	long num_gc_cancelled_gc_already_happening;

	long get_num_erases_executed() { return num_erases; }
	static void set_record_statistics(bool val) { record_statistics = val; }
	vector<vector<uint> > num_erases_per_LUN;
	vector<vector<uint> > num_writes_per_LUN;
	vector<vector<uint> > num_gc_writes_per_LUN_origin;
	vector<vector<uint> > num_gc_writes_per_LUN_destination;
private:
	static StatisticsGatherer *inst;
//	Ssd & ssd;
	double compute_average_age(uint package_id, uint die_id);
//	string histogram_csv(map<double, uint> histogram);
//	string stacked_histogram_csv(vector<map<double, uint> > histograms, vector<string> names);

	vector<vector<vector<double> > > bus_wait_time_for_reads_per_LUN;
	vector<vector<uint> > num_reads_per_LUN;

	vector<vector<uint> > num_mapping_reads_per_LUN;
	vector<vector<uint> > num_mapping_writes_per_LUN;

	vector<vector<vector<double> > > bus_wait_time_for_writes_per_LUN;


	vector<vector<uint> > num_gc_reads_per_LUN;
	vector<vector<double> > sum_gc_wait_time_per_LUN;
	vector<vector<vector<double> > > gc_wait_time_per_LUN;
	vector<vector<uint> > num_copy_backs_per_LUN;

	long num_erases;
	long num_gc_writes;


	vector<vector<uint> > num_gc_scheduled_per_LUN;

	static const uint queue_length_tracker_resolution = 1000; // microseconds = 1 ms
	vector<uint> queue_length_tracker;

	vector<vector<uint> > num_executed_gc_ops;
	vector<vector<uint> > num_live_pages_in_gc_exec;

	static const double wait_time_histogram_bin_size;
	map<double, uint> wait_time_histogram_appIOs_write;
	map<double, uint> wait_time_histogram_appIOs_read;
	map<double, uint> wait_time_histogram_appIOs_write_and_read;
	map<double, uint> wait_time_histogram_non_appIOs_write;
	map<double, uint> wait_time_histogram_non_appIOs_read;
	map<double, uint> wait_time_histogram_non_appIOs_all;

	static const double io_counter_window_size;
	vector<uint> application_io_history;
	vector<uint> non_application_io_history;

	vector<double> latency_history_write;
	vector<double> latency_history_read;
	vector<double> latency_history_write_and_read;

	// garbage collection stats
	long num_gc_executed;
	double num_migrations;
	long num_gc_scheduled;

	long num_gc_targeting_package_die_class;
	long num_gc_targeting_package_die;
	long num_gc_targeting_package_class;
	long num_gc_targeting_package;
	long num_gc_targeting_class;
	long num_gc_targeting_anything;

	vector<vector<uint> > num_wl_writes_per_LUN_origin;
	vector<vector<uint> > num_wl_writes_per_LUN_destination;

	double end_time;
	static bool record_statistics;
};

// Keeps track of the fraction of the time in which channels and LUNs are busy
class Utilization_Meter {
public:
	static void init();
	static void register_event(double prev_time, double duration, Event const& event, address_valid gran);
	static void print();
	static double get_avg_channel_utilization();
	static double get_avg_LUN_utilization();
	static double get_channel_utilization(int package_id);
	static double get_LUN_utilization(int lun_id);
private:
	static vector<double> channel_used;
	static vector<double> LUNs_used;
	static vector<double> channel_unused;
	static vector<double> LUNs_unused;
};

// Keeps track of the fraction of the time in which there is free space in LUNs for writes
class Free_Space_Meter {
public:
	static void init();
	static void register_num_free_pages_for_app_writes(long num_free_pages_for_app_writes, double timestamp);
	static void print();
	static double get_current_time() { return current_time; }
private:
	static long prev_num_free_pages_for_app_writes;
	static double timestamp_of_last_change, current_time;
	static double total_time_with_free_space;
	static double total_time_without_free_space;
};

// Keeps track of the fraction of the time in which there is free space in a given LUN for new writes
class Free_Space_Per_LUN_Meter {
public:
	static void init();
	static void mark_out_of_space(Address addr, double timestamp);
	static void mark_new_space(Address addr, double timestamp);
	static void print();
private:
	static vector<double> total_time_without_free_space;
	static vector<double> total_time_with_free_space;
	static vector<double> timestamp_of_last_change;
	static vector<bool> has_free_pages;
};

class Individual_Threads_Statistics {
public:
	static void init();
	static void print();
	static void register_thread(Thread*, string name);
	static StatisticsGatherer* get_stats_for_thread(int index);
	static int size();
private:
	static vector<Thread*> threads;
	static vector<string> thread_names;
};

class Queue_Length_Statistics {
public:
	static void init();
	static void register_queue_size(int queue_size, double current_time);
	static void print_avg();
	static void print_distribution();
private:
	static map<int, long> distribution; // maps from queue size to the amount of time in which this queue size took place
	static double last_registry_time;
};

class Experiment_Result {
public:
	Experiment_Result(string experiment_name, string data_folder, string sub_folder, string variable_parameter_name);
	~Experiment_Result();
	void start_experiment();
	void collect_stats(string variable_parameter_value);
	void collect_stats(string variable_parameter_value, StatisticsGatherer* statistics_gatherer);
	void end_experiment();
	double time_elapsed() { return end_time - start_time; }

	bool experiment_started;
	bool experiment_finished;
	string experiment_name;
	string data_folder;
	string sub_folder;
	string variable_parameter_name; // e.g. "Used space (%)". Becomes x-axis
	vector<string> column_names;
	uint max_age;
	uint max_age_freq;
	string graph_filename_prefix;
	vector<double> max_waittimes;
	map<string, vector<double> > vp_max_waittimes; // Varable parameter --> max waittimes for each waittime measurement type
	//map<double, vector<uint> > vp_num_IOs; // Varable parameter --> num_ios[write, read, write+read]
    static const string throughput_column_name; // e.g. "Average throughput (IOs/s)". Becomes y-axis on aggregated (for all experiments with different values for the variable parameter) throughput graph
    static const string write_throughput_column_name;
    static const string read_throughput_column_name;
    std::ofstream* stats_file;
    double start_time;
    double end_time;
    vector<string> points;

	static const string datafile_postfix;
	static const string stats_filename;
	static const string waittime_filename_prefix;
	static const string age_filename_prefix;
	static const string queue_filename_prefix;
	static const string throughput_filename_prefix;
	static const string latency_filename_prefix;
	static const double M;
	static const double K;
};

class Workload_Definition {
public:
	Workload_Definition();
	void recalculate_lba_range();
	virtual ~Workload_Definition() {};
	vector<Thread*> generate_instance();
	virtual vector<Thread*> generate() = 0;
	void set_lba_range(long min, long max) {min_lba = min; max_lba = max;}
protected:
	long min_lba, max_lba;
};

class Grace_Hash_Join_Workload : public Workload_Definition {
public:
	Grace_Hash_Join_Workload();
	vector<Thread*> generate();
	inline void set_use_flexible_Reads(bool val) { use_flexible_reads = val; }
private:
	double r1; // Relation 1 percentage use of addresses
	double r2; // Relation 2 percentage use of addresses
	double fs; // Free space percentage use of addresses
	bool use_flexible_reads;
};

// This workload consists of 3 threads operating in parallel.
// The logical address space is divided into two equal halves.
// On the first half, one thread is doing random reads, and another is doing random writes.
// On the other half, there is a file system emulating thread that performs large sequential writes.
class File_System_With_Noise : public Workload_Definition {
public:
	vector<Thread*> generate();
};

class Synch_Random_Workload : public Workload_Definition {
public:
	vector<Thread*> generate();
};

class Random_Workload : public Workload_Definition {
public:
	Random_Workload(long num_threads);
	vector<Thread*> generate();
private:
	long num_threads;
};

class Asynch_Random_Workload : public Workload_Definition {
public:
	Asynch_Random_Workload(double writes_probability = 0.5);
	vector<Thread*> generate();
private:
	double writes_probability;
};

// This workload starts with a large sequential write of the entire logical address space
// After that an asynchronous thread performs random writes across the logical address space
class Init_Workload : public Workload_Definition {
public:
	vector<Thread*> generate();
};

// This workload conists of a large sequential write of the entire logical address space
class Init_Write : public Workload_Definition {
public:
	vector<Thread*> generate();
};

// This workload consists of a file system emulating thread that does many large file writes
class Synch_Write : public Workload_Definition {
public:
	vector<Thread*> generate();
};

class Experiment {
public:
	Experiment();
	static double CPU_time_user();
	static double CPU_time_system();
	static double wall_clock_time();
	static string pretty_time(double time);
	static void draw_graph(int sizeX, int sizeY, string outputFile, string dataFilename, string title, string xAxisTitle, string yAxisTitle, string xAxisConf, string command);
	static void draw_graph_with_histograms(int sizeX, int sizeY, string outputFile, string dataFilename, string title, string xAxisTitle, string yAxisTitle, string xAxisConf, string command, vector<string> histogram_commands);
	static void graph(int sizeX, int sizeY, string title, string filename, int column, vector<Experiment_Result> experiments, int y_max = UNDEFINED, string subfolder = "");
	//static void latency_plot(int sizeX, int sizeY, string title, string filename, int column, int variable_parameter_value, Experiment_Result experiment, int y_max = UNDEFINED);
	static void waittime_boxplot(int sizeX, int sizeY, string title, string filename, int mean_column, Experiment_Result experiment);
	static void waittime_histogram(int sizeX, int sizeY, string outputFile, Experiment_Result experiment, int black_column, int red_column = -1);
    static void cross_experiment_waittime_histogram(int sizeX, int sizeY, string outputFile, vector<Experiment_Result> experiments, string point, int black_column, int red_column = -1);
	static void age_histogram(int sizeX, int sizeY, string outputFile, Experiment_Result experiment);
	static void queue_length_history(int sizeX, int sizeY, string outputFile, Experiment_Result experiment);
	static void plot(int sizeX, int sizeY, string outputFile, Experiment_Result experiment, string csv_name, string title, string x_axis_label, string y_axis_label, int num_lines, int x_min = UNDEFINED, int x_max = UNDEFINED, int y_min = UNDEFINED, int y_max = UNDEFINED);
	static string get_working_dir();
	static void unify_under_one_statistics_gatherer(vector<Thread*> threads, StatisticsGatherer* statistics_gatherer);
	template <class T> void simple_experiment_double(string name, T* variable, T min, T max, T inc);
	//static vector<Experiment_Result> simple_experiment(Workload_Definition* experiment_workload, string name, long IO_limit, long& variable, long min_val, long max_val, long incr);
	static void simple_experiment(Workload_Definition* workload, string name, int IO_limit);
	void setup(string name);
	void run(string experiment_name);
	void run_single_point(string name);
	static vector<Experiment_Result> random_writes_on_the_side_experiment(Workload_Definition* workload, int write_threads_min, int write_threads_max, int write_threads_inc, string name, int IO_limit, double used_space, int random_writes_min_lba, int random_writes_max_lba);
	static Experiment_Result copyback_experiment(vector<Thread*> (*experiment)(int highest_lba), int used_space, int max_copybacks, string data_folder, string name, int IO_limit);
	static Experiment_Result copyback_map_experiment(vector<Thread*> (*experiment)(int highest_lba), int cb_map_min, int cb_map_max, int cb_map_inc, int used_space, string data_folder, string name, int IO_limit);
	void set_exponential_increase(bool e) { exponential_increase = e; }
	void draw_graphs();
	void draw_aggregate_graphs();
	void draw_experiment_spesific_graphs();
	static void save_state(OperatingSystem* os, string file_name);
	static OperatingSystem* load_state(string file_name);
	static void calibrate_and_save(Workload_Definition*, string name, int num_times_to_repeat = NUMBER_OF_ADDRESSABLE_PAGES() * 3, bool force = false);
	static void write_config_file(string folder_name);
	static void write_results_file(string folder_name);
	static void create_base_folder(string folder_name);
	static string get_base_directory() { return base_folder; }
	void set_variable(double* variable, double low, double high, double incr, string variable_name);
	void set_variable(int* variable, int low, int high, int incr, string variable_name);
	void set_workload(Workload_Definition* w) { workload = w; }
	void set_calibration_workload(Workload_Definition* w) { calibrate_for_each_point = true; calibration_workload = w; }
	void set_io_limit(int limit) { io_limit = limit; };
	void set_calibration_file(string file) { calibration_file = file; }
	void set_generate_trace_files(bool val) {generate_trace_file = val;}
	void set_alternate_location_for_results_file(string val) { alternate_location_for_results_file = val; }
private:
	string variable_name;
	double* d_variable;
	double d_min, d_max, d_incr;

	int* i_variable;
	int i_min, i_max, i_incr;

	int io_limit;
	Workload_Definition* workload;
	Workload_Definition* calibration_workload;
	vector<vector<Experiment_Result> > results;
	string calibration_file;
	bool calibrate_for_each_point;
	bool generate_trace_file;

	string alternate_location_for_results_file;

	static void multigraph(int sizeX, int sizeY, string outputFile, vector<string> commands, vector<string> settings = vector<string>(), int x_min = UNDEFINED, int x_max = UNDEFINED, int y_min = UNDEFINED, int y_max = UNDEFINED);

	static uint max_age;
	static const bool REMOVE_GLE_SCRIPTS_AGAIN;
	//static const string experiments_folder = "./Experiments/";
	static const string markers[];
	static const double M; // One million
	static const double K; // One thousand
	static double calibration_precision;      // microseconds
	static double calibration_starting_point; // microseconds
	static string base_folder;
	bool exponential_increase;
};

class MTRand_int32 { // Mersenne Twister random number generator
public:
// default constructor: uses default seed only if this is the first instance
/* MKS: Uncommented empty constructor since seeding is required for each object instantiation as a result of the removal of static fields */
  MTRand_int32() : n(624), m(397), state(n) { if (!init) seed(5489UL); init = true; }
// constructor with 32 bit int as seed
  MTRand_int32(unsigned long s) : n(624), m(397), state(n) { seed(s); init = true; }
// constructor with array of size 32 bit ints as seed
  MTRand_int32(const unsigned long* array, int size) : n(624), m(397), state(n) { seed(array, size); init = true; }
// the two seed functions
  void seed(unsigned long); // seed with 32 bit integer
  void seed(const unsigned long*, int size); // seed with array
// overload operator() to make this a generator (functor)
  unsigned long operator()() { return rand_int32(); }
// 2007-02-11: made the destructor virtual; thanks "double more" for pointing this out
  virtual ~MTRand_int32() {} // destructor

  friend class boost::serialization::access;
  template<class Archive>
  void serialize(Archive & ar, const unsigned int version) {
  	ar & state;
  	ar & p;
  	ar & init;
  }

protected: // used by derived classes, otherwise not accessible; use the ()-operator
  unsigned long rand_int32(); // generate 32 bit random integer
private:
  int n, m; // compile time constants
// the variables below are static (no duplicates can exist)
  // MKS: Following three lines uncommented because the static fields are made dynamic
  /*static*/ vector<uint> state; // state vector array
  /*static*/ int p; // position in state array
  /*static*/ bool init; // true if init function is called
// private functions used to generate the pseudo random numbers
  unsigned long twiddle(unsigned long, unsigned long); // used by gen_state()
  void gen_state(); // generate new state
// make copy constructor and assignment operator unavailable, they don't make sense
  //MTRand_int32(const MTRand_int32&); // copy constructor not defined
  void operator=(const MTRand_int32&); // assignment operator not defined

};

// inline for speed, must therefore reside in header file
inline unsigned long MTRand_int32::twiddle(unsigned long u, unsigned long v) {
  return (((u & 0x80000000UL) | (v & 0x7FFFFFFFUL)) >> 1)
    ^ ((v & 1UL) ? 0x9908B0DFUL : 0x0UL);
}

inline unsigned long MTRand_int32::rand_int32() { // generate 32 bit random int
  /*DEBUG LINE INSERTED BY MK*/assert(init == true);
  if (p == n) gen_state(); // new state vector needed
// gen_state() is split off to be non-inline, because it is only called once
// in every 624 calls and otherwise irand() would become too big to get inlined
  unsigned long x = state[p++];
  x ^= (x >> 11);
  x ^= (x << 7) & 0x9D2C5680UL;
  x ^= (x << 15) & 0xEFC60000UL;
  return x ^ (x >> 18);
}

// generates double floating point numbers in the half-open interval [0, 1)
class MTRand : public MTRand_int32 {
public:
  MTRand() : MTRand_int32() {}
  MTRand(unsigned long seed) : MTRand_int32(seed) {}
  MTRand(const unsigned long* seed, int size) : MTRand_int32(seed, size) {}
  ~MTRand() {}
  double operator()() {
    return static_cast<double>(rand_int32()) * (1. / 4294967296.); } // divided by 2^32

  /*friend class boost::serialization::access;
  template<class Archive>
  void serialize(Archive & ar, const unsigned int version) {
	  ar & boost::serialization::base_object<MTRand_int32>(*this);
  }*/

private:
  MTRand(const MTRand&); // copy constructor not defined
  void operator=(const MTRand&); // assignment operator not defined
};

// generates double floating point numbers in the closed interval [0, 1]
class MTRand_closed : public MTRand_int32 {
public:
  MTRand_closed() : MTRand_int32() {}
  MTRand_closed(unsigned long seed) : MTRand_int32(seed) {}
  MTRand_closed(const unsigned long* seed, int size) : MTRand_int32(seed, size) {}
  ~MTRand_closed() {}
  double operator()() {
    return static_cast<double>(rand_int32()) * (1. / 4294967295.); } // divided by 2^32 - 1
private:
  MTRand_closed(const MTRand_closed&); // copy constructor not defined
  void operator=(const MTRand_closed&); // assignment operator not defined

  /*friend class boost::serialization::access;
  template<class Archive>
  void serialize(Archive & ar, const unsigned int version) {
	  ar & boost::serialization::base_object<MTRand_int32>(*this);
  }*/
};

// generates double floating point numbers in the open interval (0, 1)
class MTRand_open : public MTRand_int32 {
public:
  MTRand_open() : MTRand_int32() {}
  MTRand_open(unsigned long seed) : MTRand_int32(seed) {}
  MTRand_open(const unsigned long* seed, int size) : MTRand_int32(seed, size) {}
  ~MTRand_open() {}
  double operator()() {
    return (static_cast<double>(rand_int32()) + .5) * (1. / 4294967296.); } // divided by 2^32
private:
  MTRand_open(const MTRand_open&); // copy constructor not defined
  void operator=(const MTRand_open&); // assignment operator not defined

  /*friend class boost::serialization::access;
  template<class Archive>
  void serialize(Archive & ar, const unsigned int version) {
	  ar & boost::serialization::base_object<MTRand_int32>(*this);
  }*/
};

// generates 53 bit resolution doubles in the half-open interval [0, 1)
class MTRand53 : public MTRand_int32 {
public:
  MTRand53() : MTRand_int32() {}
  MTRand53(unsigned long seed) : MTRand_int32(seed) {}
  MTRand53(const unsigned long* seed, int size) : MTRand_int32(seed, size) {}
  ~MTRand53() {}
  double operator()() {
    return (static_cast<double>(rand_int32() >> 5) * 67108864. +
      static_cast<double>(rand_int32() >> 6)) * (1. / 9007199254740992.); }
private:
  MTRand53(const MTRand53&); // copy constructor not defined
  void operator=(const MTRand53&); // assignment operator not defined

  /*friend class boost::serialization::access;
  template<class Archive>
  void serialize(Archive & ar, const unsigned int version) {
	  ar & boost::serialization::base_object<MTRand_int32>(*this);
  }*/
};

};
#endif
