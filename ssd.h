
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
#include <set>
#include <algorithm>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include "bloom_filter.hpp"
#include <sys/types.h>
#include "MTRand/mtrand.h" // Marsenne Twister random number generator
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
void load_config(void);
void print_config(FILE *stream);

/* Ram class:
 * 	delay to read from and write to the RAM for 1 page of data */
extern const double RAM_READ_DELAY;
extern const double RAM_WRITE_DELAY;

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

/*
 * Mapping directory
 */
extern const uint MAP_DIRECTORY_SIZE;

/*
 * FTL Implementation
 */
extern const uint FTL_IMPLEMENTATION;

/*
 * LOG page limit for BAST.
 */
extern const uint BAST_LOG_PAGE_LIMIT;

/*
 * LOG page limit for FAST.
 */
extern const uint FAST_LOG_PAGE_LIMIT;

/*
 * Number of blocks allowed to be in DFTL Cached Mapping Table.
 */
extern const uint CACHE_DFTL_LIMIT;

/*
 * Parallelism mode
 */
extern const uint PARALLELISM_MODE;

/* Virtual block size (as a multiple of the physical block size) */
extern const uint VIRTUAL_BLOCK_SIZE;

/* Virtual page size (as a multiple of the physical page size) */
extern const uint VIRTUAL_PAGE_SIZE;

// extern const uint NUMBER_OF_ADDRESSABLE_BLOCKS;
static inline uint NUMBER_OF_ADDRESSABLE_BLOCKS() {
	return (SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE) / VIRTUAL_PAGE_SIZE;
}

/* RAISSDs: Number of physical SSDs */
extern const uint RAID_NUMBER_OF_PHYSICAL_SSDS;

/*
 * Memory area to support pages with data.
 */
extern void *page_data;
extern void *global_buffer;

/*
 * Controls the block manager to be used
 */
extern int BLOCK_MANAGER_ID;
extern int GREED_SCALE;
extern int WEARWOLF_LOCALITY_THRESHOLD;
extern bool ENABLE_TAGGING;

/*
 * Controls the level of detail of output
 */
extern int PRINT_LEVEL;
extern bool PRINT_FILE_MANAGER_INFO;
/*
 * tells the Operating System class to either lock or not lock LBAs after dispatching IOs to them
 */
extern const bool OS_LOCK;

/* Defines the max number of copy back operations on a page before ECC check is performed.
 * Set to zero to disable copy back GC operations */
extern uint MAX_REPEATED_COPY_BACKS_ALLOWED;

/* Defines the max number of page addresses in map keeping track of each pages copy back count */
extern uint MAX_ITEMS_IN_COPY_BACK_MAP;

/* Defines the maximal length of the SSD queue  */
extern int MAX_SSD_QUEUE_SIZE;

/* Defines the maximal number of locks that can be held by the OS  */
extern uint MAX_OS_NUM_LOCKS;

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
enum event_type{NOT_VALID, READ, READ_COMMAND, READ_TRANSFER, WRITE, ERASE, MERGE, TRIM, GARBAGE_COLLECTION, COPY_BACK};

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
 * Block type status
 * used for the garbage collector specify what pool
 * it should work with.
 * the block types are log, data and map (Directory map usually)
 */
enum block_type {LOG, DATA, LOG_SEQ};

/*
 * Enumeration of the different FTL implementations.
 */
enum ftl_implementation {IMPL_PAGE, IMPL_BAST, IMPL_FAST, IMPL_DFTL, IMPL_BIMODAL};

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
class Channel;
class Bus;
class Page;
class Block;
class Plane;
class Die;
class Package;

class FtlParent;
class FtlImpl_Page;
class FtlImpl_Bast;
class FtlImpl_Fast;
class FtlImpl_DftlParent;
class FtlImpl_Dftl;
class FtlImpl_BDftl;

class Ram;
class Controller;
class Ssd;

class event_queue;
class IOScheduler;
class Scheduling_Strategy;
class Simple_Scheduling_Strategy;

class Block_manager_parent;
class Block_manager_parallel;
class Shortest_Queue_Hot_Cold_BM;
class Wearwolf;
class Wearwolf_Locality;
class Wear_Leveling_Strategy;
class Garbage_Collector;

class Sequential_Pattern_Detector;
class Page_Hotness_Measurer;
class Random_Order_Iterator;

class OperatingSystem;
class Thread;
class Synchronous_Writer;

struct Address_Range;
class Flexible_Reader;

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
	ulong real_address;
	enum address_valid valid;
	Address(void);
	inline Address(const Address &address) { *this = address; }
	inline Address(const Address *address) { *this = *address; }
	Address(uint package, uint die, uint plane, uint block, uint page, enum address_valid valid);
	Address(uint address, enum address_valid valid);
	~Address();
	enum address_valid check_valid(uint ssd_size = SSD_SIZE, uint package_size = PACKAGE_SIZE, uint die_size = DIE_SIZE, uint plane_size = PLANE_SIZE, uint block_size = BLOCK_SIZE);
	enum address_valid compare(const Address &address) const;
	void print(FILE *stream = stdout) const;

	void operator+(int);
	void operator+(uint);
	Address &operator+=(const uint rhs);
	inline Address &operator=(const Address &rhs) {
		if(this == &rhs)
			return *this;
		package = rhs.package;
		die = rhs.die;
		plane = rhs.plane;
		block = rhs.block;
		page = rhs.page;
		valid = rhs.valid;
		real_address = rhs.real_address;
		return *this;
	}

	void set_linear_address(ulong address, enum address_valid valid);
	void set_linear_address(ulong address);
	ulong get_linear_address() const;
};

/* Class to emulate a log block with page-level mapping. */
class LogPageBlock
{
public:
	LogPageBlock(void);
	~LogPageBlock(void);

	int *pages;
	long *aPages;
	Address address;
	int numPages;

	LogPageBlock *next;

	bool operator() (const ssd::LogPageBlock& lhs, const ssd::LogPageBlock& rhs) const;
	bool operator() (const ssd::LogPageBlock*& lhs, const ssd::LogPageBlock*& rhs) const;
};


/* Class to manage I/O requests as events for the SSD.  It was designed to keep
 * track of an I/O request by storing its type, addressing, and timing.  The
 * SSD class creates an instance for each I/O request it receives. */
class Event 
{
public:
	Event(enum event_type type, ulong logical_address, uint size, double start_time);
	Event();
	Event(Event& event);
	virtual ~Event() {}
	inline ulong get_logical_address() const 			{ return logical_address; }
	inline void set_logical_address(ulong addr) 		{ logical_address = addr; }
	inline const Address &get_address() const 			{ return address; }
	inline const Address &get_replace_address() const 	{ return replace_address; }
	inline uint get_size() const 						{ return size; }
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
	inline bool is_experiment_io() const 				{ return experiment_io; }
	inline void set_experiment_io(bool val) 			{ experiment_io = val; }
	inline void set_tag(int new_tag) 					{ tag = new_tag; }
	inline void set_thread_id(int new_thread_id)		{ thread_id = new_thread_id; }
	inline void set_address(const Address &address) {
		if (type == WRITE || type == READ || type == READ_COMMAND || type == READ_TRANSFER) {
			assert(address.valid == PAGE);
		}
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
protected:
	double start_time;
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

	bool experiment_io;

	// an ID for a single IO to the chip. This is not actually used for any logical purpose
	static uint id_generator;
	uint id;

	// an ID to manage dependencies in the scheduler.
	uint application_io_id;
	static uint application_io_id_generator;

	int age_class;
	int tag;

	int thread_id;
	double pure_ssd_wait_time;
};

/* Single bus channel
 * Simulate multiple devices on 1 bus channel with variable bus transmission
 * durations for data and control delays with the Channel class.  Provide the 
 * delay times to send a control signal or 1 page of data across the bus
 * channel, the bus table size for the maximum number channel transmissions that
 * can be queued, and the maximum number of devices that can connect to the bus.
 * To elaborate, the table size is the size of the channel scheduling table that
 * holds start and finish times of events that have not yet completed in order
 * to determine where the next event can be scheduled for bus utilization. */
class Channel
{
public:
	Channel(Ssd* ssd, double ctrl_delay = BUS_CTRL_DELAY, double data_delay = BUS_DATA_DELAY, uint table_size = BUS_TABLE_SIZE, uint max_connections = BUS_MAX_CONNECT);
	~Channel(void);
	enum status lock(double start_time, double duration, Event &event);
	double get_currently_executing_operation_finish_time();
	double ready_time(void);
private:
	//void unlock(double current_time);

	struct lock_times {
		double lock_time;
		double unlock_time;
		int event_id;
	};

	double currently_executing_operation_finish_time;
	Ssd* ssd;
};

/* Multi-channel bus comprised of Channel class objects
 * Simulates control and data delays by allowing variable channel lock
 * durations.  The sender (controller class) should specify the delay (control,
 * data, or both) for events (i.e. read = ctrl, ctrl+data; write = ctrl+data;
 * erase or merge = ctrl).  The hardware enable signals are implicitly
 * simulated by the sender locking the appropriate bus channel through the lock
 * method, then sending to multiple devices by calling the appropriate method
 * in the Package class. */
class Bus
{
public:
	Bus(Ssd* ssd, uint num_channels = SSD_SIZE, double ctrl_delay = BUS_CTRL_DELAY, double data_delay = BUS_DATA_DELAY, uint table_size = BUS_TABLE_SIZE, uint max_connections = BUS_MAX_CONNECT);
	~Bus(void);
	enum status lock(uint channel, double start_time, double duration, Event &event);
	inline Channel &get_channel(uint channel) { assert(channels != NULL && channel < num_channels); return channels[channel]; }
	double ready_time(uint channel);
private:
	uint num_channels;
	Channel * const channels;
};



/* The page is the lowest level data storage unit that is the size unit of
 * requests (events).  Pages maintain their state as events modify them. */
class Page 
{
public:
	inline Page() : state(EMPTY) {}
	inline ~Page(void) {}
	enum status _read(Event &event);
	enum status _write(Event &event);
	enum page_state get_state(void) const;
	void set_state(enum page_state state);
private:
	enum page_state state;
};

/* The block is the data storage hardware unit where erases are implemented.
 * Blocks maintain wear statistics for the FTL. */
class Block 
{
public:
	Block(long physical_address = 0);
	~Block(void);
	enum status read(Event &event);
	enum status write(Event &event);
	enum status _erase(Event &event);
	uint get_pages_valid(void) const;
	uint get_pages_invalid(void) const;
	enum block_state get_state(void) const;
	void set_state(void) const;
	enum page_state get_state(uint page) const;
	enum page_state get_state(const Address &address) const;
	double get_last_erase_time(void) const;
	double get_second_last_erase_time(void) const;
	double get_modification_time(void) const;
	ulong get_erases_remaining(void) const;
	enum status get_next_page(Address &address) const;
	void invalidate_page(uint page);
	long get_physical_address(void) const;
	Block *get_pointer(void);
	const Page *getPages() const { return data; }
	ulong get_age() const;
private:
	long physical_address;
	uint pages_invalid;
	Page * const data;
	uint pages_valid;
	enum block_state state;
	ulong erases_remaining;
	double last_erase_time;
	double erase_before_last_erase_time;
	double modification_time;
};

/* The plane is the data storage hardware unit that contains blocks.*/
class Plane 
{
public:
	Plane(double reg_read_delay = PLANE_REG_READ_DELAY, double reg_write_delay = PLANE_REG_WRITE_DELAY, long physical_address = 0);
	~Plane(void);
	enum status read(Event &event);
	enum status write(Event &event);
	enum status erase(Event &event);
	double get_last_erase_time(const Address &address) const;
	ulong get_erases_remaining(const Address &address) const;
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	Block *get_block_pointer(const Address & address);
	inline Block *getBlocks() { return data; }
private:
	enum status get_next_page(void);
	Block * const data;
	ulong erases_remaining;
	double last_erase_time;
	double reg_read_delay;
	double reg_write_delay;
	Address next_page;
};

/* The die is the data storage hardware unit that contains planes and is a flash
 * chip.  Dies maintain wear statistics for the FTL. */
class Die 
{
public:
	Die(const Package &parent, Channel &channel, long physical_address = 0);
	~Die(void);
	enum status read(Event &event);
	enum status write(Event &event);
	enum status erase(Event &event);
	const Package &get_parent(void) const;
	double get_last_erase_time(const Address &address) const;
	ulong get_erases_remaining(const Address &address) const;
	double get_currently_executing_io_finish_time();
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	Block *get_block_pointer(const Address & address);
	inline Plane *getPlanes() { return data; }
	void clear_register();
	int get_last_read_application_io();
	bool register_is_busy();
private:
	void update_wear_stats(const Address &address);
	Plane * const data;
	const Package &parent;
	Channel &channel;
	uint least_worn;
	ulong erases_remaining;
	double last_erase_time;
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
	Package (Channel &channel, uint package_size = PACKAGE_SIZE, long physical_address = 0);
	~Package ();
	enum status read(Event &event);
	enum status write(Event &event);
	enum status erase(Event &event);
	const Ssd &get_parent(void) const;
	double get_last_erase_time (const Address &address) const;
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	Block *get_block_pointer(const Address & address);
	inline Die *getDies() { return data; }
private:
	uint size;
	Die * const data;
	uint least_worn;
	double last_erase_time;
};

const int UNDEFINED = -1;
const int INFINITE = std::numeric_limits<long>::max();

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
	~BloomFilter_Page_Hotness_Measurer(void);
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
	Random_Order_Iterator();
	vector<int> get_iterator(int needed_length);
private:
	void shuffle(vector<int>&);
	MTRand_int32 random_number_generator;
};

class FtlParent
{
public:
	FtlParent(Ssd &ssd) : ssd(ssd), scheduler(NULL) {};
	inline void set_scheduler(IOScheduler* sched) { scheduler = sched; }
	virtual ~FtlParent () {};
	virtual void read(Event *event) = 0;
	virtual void write(Event *event) = 0;
	virtual void trim(Event *event) = 0;
	virtual void register_write_completion(Event const& event, enum status result) = 0;
	virtual void register_read_completion(Event const& event, enum status result) = 0;
	virtual void register_trim_completion(Event & event) = 0;
	virtual long get_logical_address(uint physical_address) const = 0;
	virtual Address get_physical_address(uint logical_address) const = 0;
	virtual void set_replace_address(Event& event) const = 0;
	virtual void set_read_address(Event& event) = 0;
protected:
	Ssd &ssd;
	IOScheduler *scheduler;
};

class FtlImpl_Page : public FtlParent
{
public:
	FtlImpl_Page(Ssd &ssd);
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
	void set_read_address(Event& event);
private:
	Address get_physical_address(Event const& event) const;
	vector<long> logical_to_physical_map;
	vector<long> physical_to_logical_map;
};


class FtlImpl_DftlParent : public FtlParent
{
public:
	FtlImpl_DftlParent(Ssd &ssd);
	~FtlImpl_DftlParent();
	virtual void read(Event *event) = 0;
	virtual void write(Event *event) = 0;
	virtual void trim(Event *event) = 0;
protected:
	struct MPage {
		long vpn;
		long ppn;
		double create_ts;		//when its added to the CTM
		double modified_ts;		//when its modified within the CTM
		bool cached;
		MPage(long vpn);
		bool has_been_modified();
	};

	long int cmt;

	static double mpage_modified_ts_compare(const MPage& mpage);

	typedef boost::multi_index_container<
		FtlImpl_DftlParent::MPage,
			boost::multi_index::indexed_by<
		    // sort by MPage::operator<
    			boost::multi_index::random_access<>,

    			// Sort by modified ts
    			boost::multi_index::ordered_non_unique<boost::multi_index::global_fun<const FtlImpl_DftlParent::MPage&,double,&FtlImpl_DftlParent::mpage_modified_ts_compare> >
		  >
		> trans_set;

	typedef trans_set::nth_index<0>::type MpageByID;
	typedef trans_set::nth_index<1>::type MpageByModified;
	trans_set trans_map;
	long *reverse_trans_map;

	void consult_GTD(long dppn, Event *event);
	void reset_MPage(FtlImpl_DftlParent::MPage &mpage);
	void resolve_mapping(Event *event, bool isWrite);
	void update_translation_map(FtlImpl_DftlParent::MPage &mpage, long ppn);
	bool lookup_CMT(long dlpn, Event *event);
	void evict_page_from_cache(double time);
	long get_logical_address(uint physical_address) const;
	long get_mapping_virtual_address(long event_lba);
	void update_mapping_on_flash(long lba, double time);
	void remove_from_cache(long lba);

	// Mapping information
	const int addressSize;
	const int addressPerPage;
	const int num_mapping_pages;
	const uint totalCMTentries;

	deque<Event*> current_dependent_events;
	map<long, long> global_translation_directory; // a map from virtual translation pages to physical translation pages
	map<long, vector<long> > ongoing_mapping_reads; // maps the address of ongoing mapping reads to LBAs that need to be inserted into the cache

	long num_pages_written;
};

class FtlImpl_Dftl : public FtlImpl_DftlParent
{
public:
	FtlImpl_Dftl(Ssd &ssd);
	~FtlImpl_Dftl();
	void read(Event *event);
	void write(Event *event);
	void trim(Event *event);
	void register_write_completion(Event const& event, enum status result);
	void register_read_completion(Event const& event, enum status result);
	void register_trim_completion(Event & event);
	void set_replace_address(Event& event) const;
	void set_read_address(Event& event);
private:
	const double over_provisioning_percentage;
};

class FtlImpl_BDftl : public FtlImpl_DftlParent
{
public:
	FtlImpl_BDftl(Controller &controller);
	~FtlImpl_BDftl();
	enum status read(Event &event);
	enum status write(Event &event);
	enum status trim(Event &event);
	void cleanup_block(Event &event, Block *block);
private:
	struct BPage {
		uint pbn;
		unsigned char nextPage;
		bool optimal;

		BPage();
	};

	BPage *block_map;
	bool *trim_map;

	queue<Block*> blockQueue;

	Block* inuseBlock;
	bool block_next_new();
	long get_free_biftl_page(Event &event);
	void print_ftl_statistics();
};


/* This is a basic implementation that only provides delay updates to events
 * based on a delay value multiplied by the size (number of pages) needed to
 * be written. */
class Ram 
{
public:
	Ram(double read_delay = RAM_READ_DELAY, double write_delay = RAM_WRITE_DELAY);
	~Ram(void);
	enum status read(Event &event);
	enum status write(Event &event);
private:
	double read_delay;
	double write_delay;
};

/* The SSD is the single main object that will be created to simulate a real
 * SSD.  Creating a SSD causes all other objects in the SSD to be created.  The
 * event_arrive method is where events will arrive from DiskSim. */
class Ssd
{
public:
	Ssd (uint ssd_size = SSD_SIZE);
	~Ssd();
	void event_arrive(Event* event);
	void event_arrive(enum event_type type, ulong logical_address, uint size, double start_time);
	void event_arrive(enum event_type type, ulong logical_address, uint size, double start_time, void *buffer);
	void progress_since_os_is_waiting();
	void register_event_completion(Event * event);
	void *get_result_buffer();
	friend class Controller;
	friend class IOScheduler;
	friend class Block_manager_parent;
	inline Package* getPackages() { return data; }
	void set_operating_system(OperatingSystem* os);
	FtlParent& get_ftl() const;
	enum status issue(Event *event);
private:
	enum status read(Event &event);
	enum status write(Event &event);
	enum status erase(Event &event);
	Package &get_data(void);

	uint size;
	Ram ram;
	Bus bus;
	Package * const data;
	double last_io_submission_time;
	OperatingSystem* os;
	FtlParent *ftl;
	IOScheduler *scheduler;
};

class RaidSsd
{
public:
	RaidSsd (uint ssd_size = SSD_SIZE);
	~RaidSsd(void);
	double event_arrive(enum event_type type, ulong logical_address, uint size, double start_time);
	double event_arrive(enum event_type type, ulong logical_address, uint size, double start_time, void *buffer);
	void *get_result_buffer();
	friend class Controller;
	void print_statistics();
	void reset_statistics();
	void write_statistics(FILE *stream);
	void write_header(FILE *stream);
	const Controller &get_controller(void) const;

	void print_ftl_statistics();
private:
	uint size;
	Ssd *Ssds;

};

class VisualTracer
{
public:
	static VisualTracer *get_instance();
	static void init();
	void register_completed_event(Event& event);
	void print_horizontally(int last_how_many_characters = UNDEFINED);
	void print_horizontally_with_breaks();
	void print_vertically();
private:
	static VisualTracer *inst;
	VisualTracer();
	~VisualTracer();
	void write(int package, int die, char symbol, int length);
	void write_with_id(int package, int die, char symbol, int length, vector<vector<char> > symbols);
	vector<vector<vector<char> > > trace;
};

class StateVisualiser
{
public:
	static void print_page_status();
	static void print_block_ages();
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

class StatisticsGatherer
{
public:
	static StatisticsGatherer *get_global_instance();
	static void init(Ssd * ssd);

	StatisticsGatherer(/*Ssd & ssd*/);
	~StatisticsGatherer();

	void register_completed_event(Event const& event);
	void register_scheduled_gc(Event const& gc);
	void register_executed_gc(Event const& gc, Block const& victim);
	void register_events_queue_length(uint queue_size, double time);
	void print();
	void print_gc_info();
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
	uint total_reads();
	uint total_writes();

	long num_gc_cancelled_no_candidate;
	long num_gc_cancelled_not_enough_free_space;
	long num_gc_cancelled_gc_already_happening;

	long get_num_erases_executed() { return num_erases; }
private:
	static StatisticsGatherer *inst;
//	Ssd & ssd;
	double compute_average_age(uint package_id, uint die_id);
//	string histogram_csv(map<double, uint> histogram);
//	string stacked_histogram_csv(vector<map<double, uint> > histograms, vector<string> names);

	vector<vector<vector<double> > > bus_wait_time_for_reads_per_LUN;
	vector<vector<uint> > num_reads_per_LUN;

	vector<vector<vector<double> > > bus_wait_time_for_writes_per_LUN;
	vector<vector<uint> > num_writes_per_LUN;

	vector<vector<uint> > num_gc_reads_per_LUN;
	vector<vector<uint> > num_gc_writes_per_LUN_origin;
	vector<vector<uint> > num_gc_writes_per_LUN_destination;
	vector<vector<double> > sum_gc_wait_time_per_LUN;
	vector<vector<vector<double> > > gc_wait_time_per_LUN;
	vector<vector<uint> > num_copy_backs_per_LUN;

	long num_erases;
	long num_gc_writes;
	vector<vector<uint> > num_erases_per_LUN;

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

	bool expleriment_started;
	double start_time;
	double end_time;
};

class ExperimentResult {
public:
	ExperimentResult(string experiment_name, string data_folder, string sub_folder, string variable_parameter_name);
	~ExperimentResult();
	void start_experiment();
	void collect_stats(uint variable_parameter_value, double os_runtime);
	void collect_stats(uint variable_parameter_value, double os_runtime, StatisticsGatherer* statistics_gatherer);
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
	vector<double> max_waittimes;
	map<uint, vector<double> > vp_max_waittimes; // Varable parameter --> max waittimes for each waittime measurement type
	map<uint, vector<uint> > vp_num_IOs; // Varable parameter --> num_ios[write, read, write+read]
    static const string throughput_column_name; // e.g. "Average throughput (IOs/s)". Becomes y-axis on aggregated (for all experiments with different values for the variable parameter) throughput graph
    static const string write_throughput_column_name;
    static const string read_throughput_column_name;
    std::ofstream* stats_file;
    string working_dir;
    double start_time;
    double end_time;
    string graph_filename_prefix;

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
	virtual vector<Thread*> generate_instance() = 0;
};

class Grace_Hash_Join_Workload : public Workload_Definition {
public:
	Grace_Hash_Join_Workload(long min_lba, long highest_lba);
	vector<Thread*> generate_instance();
	inline void set_use_flexible_Reads(bool val) { use_flexible_reads = val; }
private:
	long min_lba, max_lba;
	double r1; // Relation 1 percentage use of addresses
	double r2; // Relation 2 percentage use of addresses
	double fs; // Free space percentage use of addresses
	bool use_flexible_reads;
};

class Random_Workload : public Workload_Definition {
public:
	Random_Workload(long min_lba, long highest_lba, long num_threads);
	vector<Thread*> generate_instance();
private:
	long min_lba, max_lba, num_threads;
};

class Experiment_Runner {
public:
	static double CPU_time_user();
	static double CPU_time_system();
	static double wall_clock_time();
	static string pretty_time(double time);
	static void draw_graph(int sizeX, int sizeY, string outputFile, string dataFilename, string title, string xAxisTitle, string yAxisTitle, string xAxisConf, string command);
	static void draw_graph_with_histograms(int sizeX, int sizeY, string outputFile, string dataFilename, string title, string xAxisTitle, string yAxisTitle, string xAxisConf, string command, vector<string> histogram_commands);
	static void graph(int sizeX, int sizeY, string title, string filename, int column, vector<ExperimentResult> experiments, int y_max = UNDEFINED, string subfolder = "");
	static void latency_plot(int sizeX, int sizeY, string title, string filename, int column, int variable_parameter_value, ExperimentResult experiment, int y_max = UNDEFINED);
	static void waittime_boxplot(int sizeX, int sizeY, string title, string filename, int mean_column, ExperimentResult experiment);
	static void waittime_histogram(int sizeX, int sizeY, string outputFile, ExperimentResult experiment, vector<int> points, int black_column, int red_column = -1);
    static void cross_experiment_waittime_histogram(int sizeX, int sizeY, string outputFile, vector<ExperimentResult> experiments, int point, int black_column, int red_column = -1);
	static void age_histogram(int sizeX, int sizeY, string outputFile, ExperimentResult experiment, vector<int> points);
	static void queue_length_history(int sizeX, int sizeY, string outputFile, ExperimentResult experiment, vector<int> points);
	static void throughput_history(int sizeX, int sizeY, string outputFile, ExperimentResult experiment, vector<int> points);
	static string get_working_dir();

	static void unify_under_one_statistics_gatherer(vector<Thread*> threads, StatisticsGatherer* statistics_gatherer);

	static ExperimentResult overprovisioning_experiment(vector<Thread*> (*experiment)(int highest_lba), int space_min, int space_max, int space_inc, string data_folder, string name, int IO_limit);
	static vector<ExperimentResult> simple_experiment(Workload_Definition* workload, string data_folder, string name, int IO_limit, int& variable, int min_val, int max_val, int incr);
	static vector<ExperimentResult> random_writes_on_the_side_experiment(Workload_Definition* workload, int write_threads_min, int write_threads_max, int write_threads_inc, string data_folder, string name, int IO_limit, double used_space, int random_writes_min_lba, int random_writes_max_lba);
	static ExperimentResult copyback_experiment(vector<Thread*> (*experiment)(int highest_lba), int used_space, int max_copybacks, string data_folder, string name, int IO_limit);
	static ExperimentResult copyback_map_experiment(vector<Thread*> (*experiment)(int highest_lba), int cb_map_min, int cb_map_max, int cb_map_inc, int used_space, string data_folder, string name, int IO_limit);

	static string graph_filename_prefix;
	static void draw_graphs(vector<vector<ExperimentResult> > results, string exp_folder);
	static void draw_experiment_spesific_graphs(vector<vector<ExperimentResult> > results, string exp_folder, vector<int> x_vals);
private:
	static void multigraph(int sizeX, int sizeY, string outputFile, vector<string> commands, vector<string> settings = vector<string>());

	static uint max_age;
	static const bool REMOVE_GLE_SCRIPTS_AGAIN;
	//static const string experiments_folder = "./Experiments/";
	static const string markers[];
	static const double M; // One million
	static const double K; // One thousand
	static double calibration_precision;      // microseconds
	static double calibration_starting_point; // microseconds
};

};
#endif
