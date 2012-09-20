/* Copyright 2009, 2010 Brendan Tauras */

/* ssd.h is part of FlashSim. */

/* FlashSim is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version. */

/* FlashSim is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. */

/* You should have received a copy of the GNU General Public License
 * along with FlashSim.  If not, see <http://www.gnu.org/licenses/>. */

/****************************************************************************/

/* ssd.h
 * Brendan Tauras 2010-07-16
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
extern bool GREEDY_GC;
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
extern const uint MAX_REPEATED_COPY_BACKS_ALLOWED;

/* Defines the max number of page addresses in map keeping track of each pages copy back count */
extern const uint MAX_ITEMS_IN_COPY_BACK_MAP;

/* Defines the maximal length of the SSD queue  */
extern uint MAX_SSD_QUEUE_SIZE;

/* Defines the maximal number of locks that can be held by the OS  */
extern uint MAX_OS_NUM_LOCKS;

/* Defines how the sequential writes detection algorithm spreads a sequential write  */
extern uint LOCALITY_PARALLEL_DEGREE;

extern bool PRIORITISE_GC;

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

class IOScheduler;

class Block_manager;
class Block_manager_parent;
class Block_manager_parallel;
class Shortest_Queue_Hot_Cold_BM;
class Wearwolf;
class Wearwolf_Locality;

class Sequential_Pattern_Detector;
class Page_Hotness_Measurer;
class Random_Order_Iterator;

class OperatingSystem;
class Thread;
class Synchronous_Writer;

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
	Address(const Address &address);
	Address(const Address *address);
	Address(uint package, uint die, uint plane, uint block, uint page, enum address_valid valid);
	Address(uint address, enum address_valid valid);
	~Address();
	enum address_valid check_valid(uint ssd_size = SSD_SIZE, uint package_size = PACKAGE_SIZE, uint die_size = DIE_SIZE, uint plane_size = PLANE_SIZE, uint block_size = BLOCK_SIZE);
	enum address_valid compare(const Address &address) const;
	void print(FILE *stream = stdout) const;

	void operator+(int);
	void operator+(uint);
	Address &operator+=(const uint rhs);
	Address &operator=(const Address &rhs);

	void set_linear_address(ulong address, enum address_valid valid);
	void set_linear_address(ulong address);
	ulong get_linear_address() const;
};

class Stats
{
public:
	// Flash Translation Layer
	long numFTLRead;
	long numFTLWrite;
	long numFTLErase;
	long numFTLTrim;

	// Garbage Collection
	long numGCRead;
	long numGCWrite;
	long numGCErase;

	// Wear-leveling
	long numWLRead;
	long numWLWrite;
	long numWLErase;

	// Log based FTL's
	long numLogMergeSwitch;
	long numLogMergePartial;
	long numLogMergeFull;

	// Page based FTL's
	long numPageBlockToPageConversion;

	// Cache based FTL's
	long numCacheHits;
	long numCacheFaults;

	// Memory consumptions (Bytes)
	long numMemoryTranslation;
	long numMemoryCache;

	long numMemoryRead;
	long numMemoryWrite;

	// Advance statictics
	double translation_overhead() const;
	double variance_of_io() const;
	double cache_hit_ratio() const;

	// Constructors, maintainance, output, etc.
	Stats(void);

	void print_statistics();
	void reset_statistics();
	void write_statistics(FILE *stream);
	void write_header(FILE *stream);
private:
	void reset();
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
	~Event() {}
	ulong get_logical_address(void) const;
	const Address &get_address(void) const;
	const Address &get_merge_address(void) const;
	const Address &get_log_address(void) const;
	const Address &get_replace_address(void) const;
	uint get_size(void) const;
	enum event_type get_event_type(void) const;
	double get_start_time(void) const;
	bool is_original_application_io(void) const;
	void set_original_application_io(bool);
	double get_time_taken(void) const;
	double get_current_time(void) const;
	double get_ssd_submission_time() const;
	uint get_application_io_id(void) const;
	double get_bus_wait_time(void) const;
	double get_os_wait_time(void) const;
	bool get_noop(void) const;
	uint get_id(void) const;
	int get_tag() const;
	void set_tag(int new_tag);
	void set_address(const Address &address);
	void set_merge_address(const Address &address);
	void set_log_address(const Address &address);
	void set_replace_address(const Address &address);
	void set_start_time(double start_time);
	void set_payload(void *payload);
	void set_event_type(const enum event_type &type);
	void set_noop(bool value);
	void set_application_io_id(uint application_io_id);
	void set_garbage_collection_op(bool value);
	void set_mapping_op(bool value);
	void set_age_class(int value);
	int get_age_class() const;
	bool is_garbage_collection_op() const;
	bool is_mapping_op() const;
	void *get_payload(void) const;
	double incr_bus_wait_time(double time);
	double incr_os_wait_time(double time);
	double incr_time_taken(double time_incr);
	double get_best_case_finish_time();
	void print(FILE *stream = stdout) const;
private:
	double start_time;
	double time_taken;
	double bus_wait_time;
	double os_wait_time;
	enum event_type type;

	ulong logical_address;
	Address address;
	Address replace_address;
	Address merge_address; // Deprecated
	Address log_address;   // Deprecated
	uint size;
	void *payload;
	bool noop;

	bool garbage_collection_op;
	bool mapping_op;
	bool original_application_io;

	// an ID for a single IO to the chip. This is not actually used for any logical purpose
	static uint id_generator;
	uint id;

	// an ID to manage dependencies in the scheduler.
	uint application_io_id;
	static uint application_io_id_generator;

	int age_class;
	int tag;
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
	enum status connect(void);
	enum status disconnect(void);
	double ready_time(void);
private:
	//void unlock(double current_time);

	struct lock_times {
		double lock_time;
		double unlock_time;
		int event_id;
	};

	//static bool timings_sorter(lock_times const& lhs, lock_times const& rhs);
	//vector<lock_times> timings;

	uint table_entries;
	uint selected_entry;
	uint num_connected;
	uint max_connections;
	double ctrl_delay;
	double data_delay;

	// Stores the highest unlock_time in the vector timings list.
	double ready_at;

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
	enum status connect(uint channel);
	enum status disconnect(uint channel);
	Channel &get_channel(uint channel);
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
	Page(const Block &parent, double read_delay = PAGE_READ_DELAY, double write_delay = PAGE_WRITE_DELAY);
	~Page(void);
	enum status _read(Event &event);
	enum status _write(Event &event);
	const Block &get_parent(void) const;
	enum page_state get_state(void) const;
	void set_state(enum page_state state);
private:
	enum page_state state;
	const Block &parent;
	double read_delay;
	double write_delay;
};

/* The block is the data storage hardware unit where erases are implemented.
 * Blocks maintain wear statistics for the FTL. */
class Block 
{
public:
	long physical_address;
	uint pages_invalid;
	Block(const Plane &parent, uint size = BLOCK_SIZE, ulong erases_remaining = BLOCK_ERASES, double erase_delay = BLOCK_ERASE_DELAY, long physical_address = 0);
	~Block(void);
	enum status read(Event &event);
	enum status write(Event &event);
	enum status replace(Event &event);
	enum status _erase(Event &event);
	const Plane &get_parent(void) const;
	uint get_pages_valid(void) const;
	uint get_pages_invalid(void) const;
	enum block_state get_state(void) const;
	void set_state(void) const;
	enum page_state get_state(uint page) const;
	enum page_state get_state(const Address &address) const;
	double get_last_erase_time(void) const;
	double get_modification_time(void) const;
	ulong get_erases_remaining(void) const;
	uint get_size(void) const;
	enum status get_next_page(Address &address) const;
	void invalidate_page(uint page);
	long get_physical_address(void) const;
	Block *get_pointer(void);
	block_type get_block_type(void) const;
	void set_block_type(block_type value);
	const Page *getPages() const;
private:
	uint size;
	Page * const data;
	const Plane &parent;
	uint pages_valid;
	enum block_state state;
	ulong erases_remaining;
	double last_erase_time;
	double erase_delay;
	double modification_time;

	block_type btype;
};

/* The plane is the data storage hardware unit that contains blocks.
 * Plane-level merges are implemented in the plane.  Planes maintain wear
 * statistics for the FTL. */
class Plane 
{
public:
	Plane(const Die &parent, uint plane_size = PLANE_SIZE, double reg_read_delay = PLANE_REG_READ_DELAY, double reg_write_delay = PLANE_REG_WRITE_DELAY, long physical_address = 0);
	~Plane(void);
	enum status read(Event &event);
	enum status write(Event &event);
	enum status erase(Event &event);
	enum status replace(Event &event);
	const Die &get_parent(void) const;
	double get_last_erase_time(const Address &address) const;
	ulong get_erases_remaining(const Address &address) const;
	uint get_size(void) const;
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	uint get_num_free(const Address &address) const;
	uint get_num_valid(const Address &address) const;
	uint get_num_invalid(const Address &address) const;
	Block *get_block_pointer(const Address & address);
	Block *getBlocks();
private:
	void update_wear_stats(void);
	enum status get_next_page(void);
	uint size;
	Block * const data;
	const Die &parent;
	ulong erases_remaining;
	double last_erase_time;
	double reg_read_delay;
	double reg_write_delay;
	Address next_page;
	uint free_blocks;
};

/* The die is the data storage hardware unit that contains planes and is a flash
 * chip.  Dies maintain wear statistics for the FTL. */
class Die 
{
public:
	Die(const Package &parent, Channel &channel, uint die_size = DIE_SIZE, long physical_address = 0);
	~Die(void);
	enum status read(Event &event);
	enum status write(Event &event);
	enum status erase(Event &event);
	enum status replace(Event &event);
	const Package &get_parent(void) const;
	double get_last_erase_time(const Address &address) const;
	ulong get_erases_remaining(const Address &address) const;
	double get_currently_executing_io_finish_time();
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	uint get_num_free(const Address &address) const;
	uint get_num_valid(const Address &address) const;
	uint get_num_invalid(const Address &address) const;
	Block *get_block_pointer(const Address & address);
	Plane *getPlanes();
	void clear_register();
	int get_last_read_application_io();
	bool register_is_busy();

private:
	void update_wear_stats(const Address &address);
	uint size;
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
	Package (const Ssd &parent, Channel &channel, uint package_size = PACKAGE_SIZE, long physical_address = 0);
	~Package ();
	enum status read(Event &event);
	enum status write(Event &event);
	enum status erase(Event &event);
	enum status replace(Event &event);
	const Ssd &get_parent(void) const;
	double get_last_erase_time (const Address &address) const;
	ulong get_erases_remaining (const Address &address) const;
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	uint get_num_free(const Address &address) const;
	uint get_num_valid(const Address &address) const;
	uint get_num_invalid(const Address &address) const;
	Block *get_block_pointer(const Address & address);
	Die *getDies();
private:
	void update_wear_stats (const Address &address);
	uint size;
	Die * const data;
	const Ssd &parent;
	uint least_worn;
	ulong erases_remaining;
	double last_erase_time;
};

const int UNDEFINED = -1;

class Page_Hotness_Measurer {
public:
//	virtual Page_Hotness_Measurer() = 0;
	virtual ~Page_Hotness_Measurer(void) {};
	virtual void register_event(Event const& event) = 0; // Inform hotness measurer about a read or write event
	virtual enum write_hotness get_write_hotness(unsigned long page_address) const = 0; // Return write hotness of a given page address
	virtual enum read_hotness get_read_hotness(unsigned long page_address) const = 0; // Return read hotness of a given page address
	virtual Address get_best_target_die_for_WC(enum read_hotness rh) const = 0; // Return address of die with leads WC data (with chosen read hotness)
};

// Simple (naïve page hotness measurer implementation)
class Simple_Page_Hotness_Measurer : public Page_Hotness_Measurer {
public:
	Simple_Page_Hotness_Measurer();
	~Simple_Page_Hotness_Measurer(void);
	void register_event(Event const& event);
	enum write_hotness get_write_hotness(unsigned long page_address) const;
	enum read_hotness get_read_hotness(unsigned long page_address) const;
	// Address get_die_with_least_wcrh() const;
	// Address get_die_with_least_wcrc() const;
	Address get_die_with_least_WC(enum read_hotness rh) const;
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
	vector<vector<uint> > num_wcrh_pages_per_die;
	vector<vector<uint> > num_wcrc_pages_per_die;
	vector<vector<double> > average_reads_per_die;
	vector<vector<uint> > current_reads_per_die;
	uint writes_counter;
	uint reads_counter;
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

class Block_manager_parent {
public:
	Block_manager_parent(Ssd& ssd, FtlParent& ftl, int classes = 1);
	virtual ~Block_manager_parent();
	virtual void register_write_outcome(Event const& event, enum status status);
	virtual void register_read_outcome(Event const& event, enum status status);
	virtual void register_erase_outcome(Event const& event, enum status status);
	virtual Address choose_address(Event const& write);
	virtual void register_write_arrival(Event const& write);
	virtual void trim(Event const& write);
	double in_how_long_can_this_event_be_scheduled(Address const& die_address, double time_taken) const;
	vector<deque<Event*> > migrate(Event * gc_event);
	bool Copy_backs_in_progress(Address const& address);

	void register_trim_making_gc_redundant();
protected:
	virtual Address choose_best_address(Event const& write) = 0;
	virtual Address choose_any_address();

	virtual void check_if_should_trigger_more_GC(double start_time);
	void increment_pointer(Address& pointer);

	bool can_write(Event const& write) const;

	void schedule_gc(double time, int package_id = -1, int die_id = -1, int klass = -1);
	vector<long> get_relevant_gc_candidates(int package_id, int die_id, int klass) const;

	Address find_free_unused_block(uint package_id, uint die_id, uint klass, double time);
	Address find_free_unused_block(uint package_id, uint die_id, double time);
	Address find_free_unused_block(uint package_id, double time);
	Address find_free_unused_block(double time);
	Address find_free_unused_block_with_class(uint klass, double time);

	void return_unfilled_block(Address block_address);

	pair<bool, pair<uint, uint> > get_free_block_pointer_with_shortest_IO_queue(vector<vector<Address> > const& dies) const;
	Address get_free_block_pointer_with_shortest_IO_queue() const;

	uint how_many_gc_operations_are_scheduled() const;

	bool has_free_pages(Address const& address) const;

	Ssd& ssd;
	FtlParent& ftl;
	vector<vector<Address> > free_block_pointers;

	map<long, uint> page_copy_back_count; // Pages that have experienced a copy-back, mapped to a count of the number of copy-backs
private:
	Block* choose_gc_victim(vector<long> candidates) const;
	void update_blocks_with_min_age(uint age);
	uint sort_into_age_class(Address const& address);
	void issue_erase(Address a, double time);
	void remove_as_gc_candidate(Address const& phys_address);
	void Wear_Level(Event const& event);

	bool copy_back_allowed_on(long logical_address);
	Address reserve_page_on(uint package, uint die, double time);
	void register_copy_back_operation_on(uint logical_address);
	void register_ECC_check_on(uint logical_address);
	vector<vector<vector<vector<Address> > > > free_blocks;  // package -> die -> class -> list of such free blocks
	vector<Block*> all_blocks;
	bool greedy_gc;
	// WL structures
	uint max_age;
	uint min_age;
	int num_age_classes;
	set<Block*> blocks_with_min_age;
	uint num_free_pages;
	uint num_available_pages_for_new_writes;
	map<int, int> blocks_being_garbage_collected;   // maps block physical address to the number of pages that still need to be migrated
	vector<vector<vector<set<long> > > > gc_candidates;  // each age class has a vector of candidates for GC
	vector<vector<uint> > num_blocks_being_garbaged_collected_per_LUN;
	Random_Order_Iterator order_randomiser;
};

// A BM that assigns each write to the die with the shortest queue. No hot-cold seperation
class Block_manager_parallel : public Block_manager_parent {
public:
	Block_manager_parallel(Ssd& ssd, FtlParent& ftl);
	~Block_manager_parallel() {}
	void register_write_outcome(Event const& event, enum status status);
	void register_erase_outcome(Event const& event, enum status status);
protected:
	Address choose_best_address(Event const& write);
};

// A simple BM that assigns writes sequentially to dies in a round-robin fashion. No hot-cold separation or anything else intelligent
class Block_manager_roundrobin : public Block_manager_parent {
public:
	Block_manager_roundrobin(Ssd& ssd, FtlParent& ftl, bool channel_alternation = true);
	~Block_manager_roundrobin();
	void register_write_outcome(Event const& event, enum status status);
	void register_erase_outcome(Event const& event, enum status status);
protected:
	Address choose_best_address(Event const& write);
private:
	void move_address_cursor();
	Address address_cursor;
	bool channel_alternation;
};

// A BM that assigns each write to the die with the shortest queue, as well as hot-cold seperation
class Shortest_Queue_Hot_Cold_BM : public Block_manager_parent {
public:
	Shortest_Queue_Hot_Cold_BM(Ssd& ssd, FtlParent& ftl);
	~Shortest_Queue_Hot_Cold_BM();
	void register_write_outcome(Event const& event, enum status status);
	void register_read_outcome(Event const& event, enum status status);
	void register_erase_outcome(Event const& event, enum status status);
protected:
	Address choose_best_address(Event const& write);
	virtual Address choose_any_address();
	void check_if_should_trigger_more_GC(double start_time);
private:
	void handle_cold_pointer_out_of_space(double start_time);
	BloomFilter_Page_Hotness_Measurer page_hotness_measurer;
	Address cold_pointer;
};


class Wearwolf : public Block_manager_parent {
public:
	Wearwolf(Ssd& ssd, FtlParent& ftl);
	~Wearwolf() {}
	virtual void register_write_outcome(Event const& event, enum status status);
	virtual void register_read_outcome(Event const& event, enum status status);
	virtual void register_erase_outcome(Event const& event, enum status status);
protected:
	virtual void check_if_should_trigger_more_GC(double start_time);
	virtual Address choose_best_address(Event const& write);
	virtual Address choose_any_address();
	BloomFilter_Page_Hotness_Measurer page_hotness_measurer;
private:
	void handle_cold_pointer_out_of_space(enum read_hotness rh, double start_time);
	void reset_any_filled_pointers(Event const& event);
	Address wcrh_pointer;
	Address wcrc_pointer;
};

class Sequential_Pattern_Detector_Listener {
public:
	virtual ~Sequential_Pattern_Detector_Listener() {}
	virtual void sequential_event_metadata_removed(long key) = 0;
};

struct sequential_writes_tracking {
	int counter, num_times_pattern_has_repeated;
	long key;
	double last_arrival_timestamp;
	double const init_timestamp;
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
};

class Wearwolf_Locality : public Wearwolf, public Sequential_Pattern_Detector_Listener {
public:
	Wearwolf_Locality(Ssd& ssd, FtlParent& ftl);
	~Wearwolf_Locality();
	void register_write_arrival(Event const& write);
	void register_write_outcome(Event const& event, enum status status);
	void register_erase_outcome(Event const& event, enum status status);
	void sequential_event_metadata_removed(long key);
protected:
	Address choose_best_address(Event const& write);
	Address choose_any_address();
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
	Address perform_sequential_write(Event const& event, long key);
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
};

class IOScheduler {
public:
	//void schedule_dependent_events_queue(deque<deque<Event*> > events);
	void schedule_events_queue(deque<Event*> events);
	void schedule_event(Event* events);
	bool is_empty();
	void finish_all_events_until_this_time(double time);
	void execute_soonest_events();
	static IOScheduler *instance();
	static void instance_initialize(Ssd& ssd, FtlParent& ftl);
	void print_stats();
	MTRand_int32 random_number_generator;
private:
	IOScheduler(Ssd& ssd, FtlParent& ftl);
	~IOScheduler();
	void setup_structures(deque<Event*> events);
	enum status execute_next(Event* event);
	void execute_current_waiting_ios();
	vector<Event*> collect_soonest_events();
	void handle_next_batch(vector<Event*>& events);
	void handle_writes(vector<Event*>& events);

	bool can_schedule_on_die(Event const* event) const;
	void handle_finished_event(Event *event, enum status outcome);
	void remove_redundant_events(Event* new_event);
	bool should_event_be_scheduled(Event* event);
	void init_event(Event* event);
	void handle_noop_events(vector<Event*>& events);

	bool remove_event_from_current_events(Event* event);

	void manage_operation_completion(Event* event);

	vector<Event*> future_events;
	vector<Event*> current_events;
	map<uint, deque<Event*> > dependencies;

	static IOScheduler *inst;
	Ssd& ssd;
	FtlParent& ftl;
	Block_manager_parent* bm;

	//map<uint, uint> LBA_to_dependencies;  // maps LBAs to dependency codes of GC operations. to be removed

	map<uint, uint> dependency_code_to_LBA;
	map<uint, event_type> dependency_code_to_type;
	map<uint, uint> LBA_currently_executing;

	map<uint, queue<uint> > op_code_to_dependent_op_codes;

	void update_current_events();
	Event* find_scheduled_event(uint dependency_code);
	void remove_current_operation(Event* event);
	void promote_to_gc(Event* event_to_promote);
	void make_dependent(Event* dependent_event, uint independent_code);
	double get_current_time() const;

	struct io_scheduler_stats {
		uint num_write_cancellations;
		io_scheduler_stats() : num_write_cancellations(0)  {}
	};
	io_scheduler_stats stats;
};

class FtlParent
{
public:
	FtlParent(Controller &controller);

	virtual ~FtlParent () {};
	virtual void read(Event *event) = 0;
	virtual void write(Event *event) = 0;
	virtual void trim(Event *event) = 0;

	virtual void print_ftl_statistics();

	friend class Block_manager;
	friend class Block_manager_parallel;

	ulong get_erases_remaining(const Address &address) const;
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	Block *get_block_pointer(const Address & address);
	Address resolve_logical_address(unsigned int logicalAddress);
	// TODO: this method should be abstract, but I am not making it so because
	// I dont't want to implement it in BAST and FAST yet
	virtual void register_write_completion(Event const& event, enum status result) = 0;
	virtual void register_read_completion(Event const& event, enum status result) = 0;
	virtual void register_trim_completion(Event & event) = 0;
	virtual long get_logical_address(uint physical_address) const = 0;
	virtual void set_replace_address(Event& event) const = 0;
	virtual void set_read_address(Event& event) = 0;
protected:
	Controller &controller;
};

class FtlImpl_Page : public FtlParent
{
public:
	FtlImpl_Page(Controller &controller);
	~FtlImpl_Page();
	void read(Event *event);
	void write(Event *event);
	void trim(Event *event);

	void register_write_completion(Event const& event, enum status result);
	void register_read_completion(Event const& event, enum status result);
	void register_trim_completion(Event & event);
	long get_logical_address(uint physical_address) const;
	void set_replace_address(Event& event) const;
	void set_read_address(Event& event);
private:
	Address get_physical_address(Event const& event) const;
	vector<long> logical_to_physical_map;
	vector<long> physical_to_logical_map;
};

class FtlImpl_Bast : public FtlParent
{
public:
	FtlImpl_Bast(Controller &controller);
	~FtlImpl_Bast();
	enum status read(Event &event);
	enum status write(Event &event);
	enum status trim(Event &event);
private:
	map<long, LogPageBlock*> log_map;

	long *data_list;

	void dispose_logblock(LogPageBlock *logBlock, long lba);
	void allocate_new_logblock(LogPageBlock *logBlock, long lba, Event &event);

	bool is_sequential(LogPageBlock* logBlock, long lba, Event &event);
	bool random_merge(LogPageBlock *logBlock, long lba, Event &event);

	void update_map_block(Event &event);

	void print_ftl_statistics();

	int addressShift;
	int addressSize;
};

class FtlImpl_Fast : public FtlParent
{
public:
	FtlImpl_Fast(Controller &controller);
	~FtlImpl_Fast();
	enum status read(Event &event);
	enum status write(Event &event);
	enum status trim(Event &event);
private:
	void initialize_log_pages();

	map<long, LogPageBlock*> log_map;

	long *data_list;
	bool *pin_list;

	bool write_to_log_block(Event &event, long logicalBlockAddress);

	void switch_sequential(Event &event);
	void merge_sequential(Event &event);
	bool random_merge(LogPageBlock *logBlock, Event &event);

	void update_map_block(Event &event);

	void print_ftl_statistics();

	long sequential_logicalblock_address;
	Address sequential_address;
	uint sequential_offset;

	uint log_page_next;
	LogPageBlock *log_pages;

	int addressShift;
	int addressSize;
};



class FtlImpl_DftlParent : public FtlParent
{
public:
	FtlImpl_DftlParent(Controller &controller);
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
	FtlImpl_Dftl(Controller &controller);
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

/* The controller accepts read/write requests through its event_arrive method
 * and consults the FTL regarding what to do by calling the FTL's read/write
 * methods.  The FTL returns an event list for the controller through its issue
 * method that the controller buffers in RAM and sends across the bus.  The
 * controller's issue method passes the events from the FTL to the SSD.
 *
 * The controller also provides an interface for the FTL to collect wear
 * information to perform wear-leveling.  */
class Controller 
{
public:
	Controller(Ssd &parent);
	~Controller(void);
	void event_arrive(Event *event);
	friend class FtlParent;
	friend class FtlImpl_Page;
	friend class FtlImpl_Bast;
	friend class FtlImpl_Fast;
	friend class FtlImpl_DftlParent;
	friend class FtlImpl_Dftl;
	friend class FtlImpl_BDftl;
	friend class Block_manager;
	friend class Block_manager_parallel;
	friend class IOScheduler;

	Stats stats;
	void print_ftl_statistics();
	FtlParent &get_ftl(void) const;
private:
	enum status issue(Event *event);
	void translate_address(Address &address);
	ulong get_erases_remaining(const Address &address) const;
	double get_last_erase_time(const Address &address) const;
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	uint get_num_free(const Address &address) const;
	uint get_num_valid(const Address &address) const;
	uint get_num_invalid(const Address &address) const;
	Block *get_block_pointer(const Address & address);
	Ssd &ssd;
	FtlParent *ftl;
};

/* The SSD is the single main object that will be created to simulate a real
 * SSD.  Creating a SSD causes all other objects in the SSD to be created.  The
 * event_arrive method is where events will arrive from DiskSim. */
class Ssd
{
public:
	Ssd (uint ssd_size = SSD_SIZE);
	~Ssd(void);
	void event_arrive(Event* event);
	void event_arrive(enum event_type type, ulong logical_address, uint size, double start_time);
	void event_arrive(enum event_type type, ulong logical_address, uint size, double start_time, void *buffer);
	void progress_since_os_is_waiting();
	void register_event_completion(Event * event);
	void *get_result_buffer();
	friend class Controller;
	friend class IOScheduler;
	friend class Block_manager_parent;
	const Controller &get_controller(void);
	Package* getPackages();
	void set_operating_system(OperatingSystem* os);
private:
	enum status read(Event &event);
	enum status write(Event &event);
	enum status erase(Event &event);
	enum status replace(Event &event);
	ulong get_erases_remaining(const Address &address) const;
	void update_wear_stats(const Address &address);
	double get_last_erase_time(const Address &address) const;	
	Package &get_data(void);
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	uint get_num_free(const Address &address) const;
	uint get_num_valid(const Address &address) const;
	uint get_num_invalid(const Address &address) const;
	Block *get_block_pointer(const Address & address);

	uint size;
	Controller controller;
	Ram ram;
	Bus bus;
	Package * const data;
	ulong erases_remaining;
	ulong least_worn;
	double last_erase_time;
	double last_io_submission_time;
	OperatingSystem* os;
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
	void register_completed_event(Event const& event);
	void print_horizontally();
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

class StatisticsGatherer
{
public:
	static StatisticsGatherer *get_instance();
	static void init(Ssd * ssd);
	void register_completed_event(Event const& event);
	void register_scheduled_gc(Event const& gc);
	void register_executed_gc(Event const& gc, Block const& victim);
	void register_events_queue_length(uint queue_size, double time);
	void print();
	void print_gc_info();
	void print_csv();
	inline double get_wait_time_histogram_bin_size() { return wait_time_histogram_bin_size; }
	inline double get_age_histogram_bin_size() { return age_histogram_bin_size; }

	string totals_csv_header();
	vector<string> totals_vector_header();
	string totals_csv_line();
	string age_histogram_csv();
	string wait_time_histogram_csv();
	string queue_length_csv();
	uint max_age();
	uint max_age_freq();
	uint total_reads();
	uint total_writes();

	long num_gc_cancelled_no_candidate;
	long num_gc_cancelled_not_enough_free_space;
	long num_gc_cancelled_gc_already_happening;

private:
	static StatisticsGatherer *inst;
	Ssd & ssd;
	StatisticsGatherer(Ssd & ssd);
	~StatisticsGatherer();
	double compute_average_age(uint package_id, uint die_id);
	string histogram_csv(map<double, uint> histogram);

	vector<vector<double> > sum_bus_wait_time_for_reads_per_LUN;
	vector<vector<vector<double> > > bus_wait_time_for_reads_per_LUN;
	vector<vector<uint> > num_reads_per_LUN;

	vector<vector<double> > sum_bus_wait_time_for_writes_per_LUN;
	vector<vector<vector<double> > > bus_wait_time_for_writes_per_LUN;
	vector<vector<uint> > num_writes_per_LUN;

	vector<vector<uint> > num_gc_reads_per_LUN;
	vector<vector<uint> > num_gc_writes_per_LUN_origin;
	vector<vector<uint> > num_gc_writes_per_LUN_destination;
	vector<vector<double> > sum_gc_wait_time_per_LUN;
	vector<vector<vector<double> > > gc_wait_time_per_LUN;
	vector<vector<uint> > num_copy_backs_per_LUN;

	vector<vector<uint> > num_erases_per_LUN;

	vector<vector<uint> > num_gc_scheduled_per_LUN;

	static const uint queue_length_tracker_resolution = 1000; // microseconds
	vector<uint> queue_length_tracker;

	vector<vector<uint> > num_executed_gc_ops;
	vector<vector<uint> > num_live_pages_in_gc_exec;

	static const double wait_time_histogram_bin_size = 250;
	static const double age_histogram_bin_size = 1;
	map<double, uint> wait_time_histogram;

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


};

class Thread
{
public:
	Thread(double time) : finished(false), time(time),
	num_pages_in_each_LUN(SSD_SIZE, vector<int>(PACKAGE_SIZE, 0)),
	num_writes_to_each_LUN(SSD_SIZE, vector<int>(PACKAGE_SIZE, 0)),
	threads_to_start_when_this_thread_finishes() {}
	virtual ~Thread();
	Event* run() {
		return finished ? NULL : issue_next_io();
	}
	void register_event_completion(Event* event);
	bool is_finished() {
		return finished;
	}
	void set_time(double current_time) {
		time = current_time;
	}
	virtual void print_thread_stats();
	void add_follow_up_thread(Thread* thread) {
		threads_to_start_when_this_thread_finishes.push_back(thread);
	}
	vector<Thread*>& get_follow_up_threads() {
		return threads_to_start_when_this_thread_finishes;
	}
protected:
	virtual Event* issue_next_io() = 0;
	virtual void handle_event_completion(Event* event) = 0;
	bool finished;
	double time;
	vector<vector<int> > num_pages_in_each_LUN;
	vector<vector<int> > num_writes_to_each_LUN;
	vector<Thread*> threads_to_start_when_this_thread_finishes;
};

class Synchronous_Sequential_Thread : public Thread
{
public:
	Synchronous_Sequential_Thread(long min_LBA, long max_LAB, int number_of_times_to_repeat, event_type type, double start_time = 1);
	Event* issue_next_io();
	void handle_event_completion(Event* event);
private:
	long min_LBA, max_LBA;
	bool ready_to_issue_next_write;
	int number_of_times_to_repeat, counter;
	event_type type;
};

class Asynchronous_Sequential_Thread : public Thread
{
public:
	Asynchronous_Sequential_Thread(long min_LBA, long max_LAB, int number_of_times_to_repeat, event_type type, double time_breaks = 20, double start_time = 1);
	Event* issue_next_io();
	void handle_event_completion(Event* event);
private:
	long min_LBA, max_LBA;
	int number_of_times_to_repeat, offset;
	bool finished_round;
	event_type type;
	int number_finished;
	double time_breaks;
};

class Synchronous_Random_Thread : public Thread
{
public:
	Synchronous_Random_Thread(long min_LBA, long max_LAB, int number_of_times_to_repeat, ulong randseed, event_type type = WRITE, double time_breaks = 0, double start_time = 1);
	Event* issue_next_io();
	void handle_event_completion(Event* event);
private:
	long min_LBA, max_LBA;
	bool ready_to_issue_next_write;
	int number_of_times_to_repeat;
	MTRand_int32 random_number_generator;
	event_type type;
	double time_breaks;
};

class Asynchronous_Random_Thread : public Thread
{
public:
	Asynchronous_Random_Thread(long min_LBA, long max_LAB, int number_of_times_to_repeat, ulong randseed = 0, event_type type = WRITE,  double time_breaks = 5, double start_time = 1);
	Event* issue_next_io();
	void handle_event_completion(Event* event);
private:
	long min_LBA, max_LBA;
	int number_of_times_to_repeat;
	MTRand_int32 random_number_generator;
	event_type type;
	double time_breaks;
};

class Collision_Free_Asynchronous_Random_Thread : public Thread
{
public:
	Collision_Free_Asynchronous_Random_Thread(long min_LBA, long max_LAB, int number_of_times_to_repeat, ulong randseed = 0, event_type type = WRITE,  double time_breaks = 5, double start_time = 1);
	Event* issue_next_io();
	void handle_event_completion(Event* event);
private:
	long min_LBA, max_LBA;
	int number_of_times_to_repeat;
	MTRand_int32 random_number_generator;
	event_type type;
	double time_breaks;
	set<long> logical_addresses_submitted;
};

/*
class Reliable_Random_Int_Generator {
public:
	Reliable_Random_Int_Generator(int seed, int num_numbers_needed);
	int next();
private:
	MTRand_int32 random_number_generator;
	deque<int> numbers;
};

class Reliable_Random_Double_Generator {
public:
	Reliable_Random_Double_Generator(int seed, int num_numbers_needed);
	double next();
private:
	MTRand_open random_number_generator;
	deque<double> numbers;
};
*/



// assuming the relation is made of contigouse pages
// RAM_available is the number of pages that fit into RAM
class External_Sort : public Thread
{
public:
	External_Sort(long relation_min_LBA, long relation_max_LBA, long RAM_available,
			long free_space_min_LBA, long free_space_max_LBA, double start_time = 1);
	Event* issue_next_io();
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

/*class Throughput_Moderator {
public:
	Throughput_Moderator();
	double register_event_completion(Event const& event);
private:
	int window_size;
	int counter;
	vector<double> window_measurments;
	double differential;
	double diff_sum;
	int breaks_counter;
	double average_wait_time;
	double last_average_wait_time;
};*/


// This is a file manager that writes one file at a time sequentially
// files might be fragmented across logical space
// files have a random size determined by the file manager
// fragmentation will eventually happen
class File_Manager : public Thread
{
public:
	File_Manager(long min_LBA, long max_LBA, uint num_files_to_write, long max_file_size, double time_breaks = 10, double start_time = 1, ulong randseed = 0);
	~File_Manager();
	Event* issue_next_io();
	void handle_event_completion(Event* event);
	virtual void print_thread_stats();
private:

	struct Address_Range {
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
	};

	struct File {
		const double death_probability;
		double time_created, time_finished_writing, time_deleted;
		static int file_id_generator;
		const uint size;
		int id;
		uint num_pages_written;
		deque<Address_Range > ranges_comprising_file;
		Address_Range current_range_being_written;
		set<long> logical_addresses_to_be_written_in_current_range;
		uint num_pages_allocated_so_far;

		File(uint size, double death_probability, double time_created);

		long get_num_pages_left_to_allocate() const;
		bool needs_new_range() const;
		bool is_finished() const;
		long get_next_lba_to_be_written();
		void register_new_range(Address_Range range);
		void register_write_completion();
		void finish(double time_finished);
		bool has_issued_last_io();
	};

	void schedule_to_trim_file(File* file);
	double generate_death_probability();
	void write_next_file(double time);
	void assign_new_range();
	void randomly_delete_files(double current_time);
	Event* issue_trim();
	Event* issue_write();
	void reclaim_file_space(File* file);
	void delete_file(File* victim, double current_time);
	void handle_file_completion(double current_time);

	long num_free_pages;
	deque<Address_Range> free_ranges;
	vector<File*> live_files;
	vector<File*> files_history;
	File* current_file;
	long min_LBA, max_LBA;
	int num_files_to_write;

	//Reliable_Random_Int_Generator random_number_generator;
	//Reliable_Random_Double_Generator double_generator;

	MTRand_int32 random_number_generator;
	MTRand_open double_generator;
	double time_breaks;
	set<long> addresses_to_trim;
	const long max_file_size;
	//Throughput_Moderator throughout_moderator;
};

struct os_event {
	int thread_id;
	Event* pending_event;
	os_event(int thread_id, Event* event) : thread_id(thread_id), pending_event(event) {}
	os_event() : thread_id(UNDEFINED), pending_event(NULL) {}
};

class OperatingSystem
{
public:
	OperatingSystem(vector<Thread*> threads);
	~OperatingSystem();
	void run();
	void register_event_completion(Event* event);
	void set_num_writes_to_stop_after(long num_writes);
	double get_total_runtime() const;
private:
	int pick_event_with_shortest_start_time();
	void dispatch_event(int thread_id);
	double get_event_minimal_completion_time(Event const*const event) const;
	bool is_LBA_locked(ulong lba);
	Ssd * ssd;
	vector<Thread*> threads;
	vector<Event*> events;

	//map<long, uint> LBA_to_thread_id_map;

	map<long, queue<uint> > write_LBA_to_thread_id;
	map<long, queue<uint> > read_LBA_to_thread_id;
	map<long, queue<uint> > trim_LBA_to_thread_id;

	map<long, queue<uint> >& get_relevant_LBA_to_thread_map(event_type);

	int currently_executing_ios_counter;
	int currently_pending_ios_counter;
	double last_dispatched_event_minimal_finish_time;

	set<uint> currently_executing_ios;
	long num_writes_to_stop_after;
	long num_writes_completed;

	double time_of_last_event_completed;
};

class Exp {
public:
	Exp(string name_, string data_folder_, string x_axis_, vector<string> column_names_, uint max_age_, uint max_age_freq_)
	:	name(name_),
	 	data_folder(data_folder_),
	 	x_axis(x_axis_),
	 	column_names(column_names_),
	 	max_age(max_age_),
	 	max_age_freq(max_age_freq_)
	{}
	string name;
	string data_folder;
	string x_axis;
	vector<string> column_names;
	uint max_age;
	uint max_age_freq;
};

class Experiment_Runner {
public:
	static double CPU_time_user();
	static double CPU_time_system();
	static double wall_clock_time();
	static string pretty_time(double time);
	static void draw_graph(int sizeX, int sizeY, string outputFile, string dataFilename, string title, string xAxisTitle, string yAxisTitle, string xAxisConf, string command);
	static void draw_graph_with_histograms(int sizeX, int sizeY, string outputFile, string dataFilename, string title, string xAxisTitle, string yAxisTitle, string xAxisConf, string command, vector<string> histogram_commands);
	static double calibrate_IO_submission_rate_queue_based(int highest_lba, vector<Thread*> (*experiment)(int highest_lba, double IO_submission_rate));
	static double measure_throughput(int highest_lba, double IO_submission_rate, vector<Thread*> (*experiment)(int highest_lba, double IO_submission_rate));
	static double calibrate_IO_submission_rate_throughput_based(int highest_lba, vector<Thread*> (*experiment)(int highest_lba, double IO_submission_rate));
	static Exp overprovisioning_experiment(vector<Thread*> (*experiment)(int highest_lba, double IO_submission_rate), int space_min, int space_max, int space_inc, string data_folder, string name);
	static void graph(int sizeX, int sizeY, string title, string filename, int column, vector<Exp> experiments);
	static void waittime_boxplot(int sizeX, int sizeY, string title, string filename, int mean_column, Exp experiment);
	static void waittime_histogram(int sizeX, int sizeY, string outputFile, Exp experiment, vector<int> points);
	static void age_histogram(int sizeX, int sizeY, string outputFile, Exp experiment, vector<int> points);
	static void queue_length_history(int sizeX, int sizeY, string outputFile, Exp experiment, vector<int> points);

private:
	static void multigraph(int sizeX, int sizeY, string outputFile, vector<string> commands, vector<string> settings = vector<string>());

	static uint max_age;
	static const bool REMOVE_GLE_SCRIPTS_AGAIN;
	//static const string experiments_folder = "./Experiments/";
	static const string datafile_postfix;
	static const string stats_filename;
	static const string waittime_filename_prefix;
	static const string age_filename_prefix;
	static const string queue_filename_prefix;
	static const string markers[];
	static const double M; // One million
	static const double K;    // One thousand
	static double calibration_precision;        // microseconds
	static double calibration_starting_point; // microseconds
};

/*class Block_manager
{
public:
	Block_manager(FtlParent *ftl);
	~Block_manager(void);

	// Usual suspects
	Address get_free_block(Event &event);
	Address get_free_block(block_type btype, Event &event);
	void invalidate(Address address, block_type btype);
	void print_statistics();
	void insert_events(Event &event);
	void promote_block(block_type to_type);
	bool is_log_full();
	void erase_and_invalidate(Event &event, Address &address, block_type btype);
	int get_num_free_blocks();

	// Used to update GC on used pages in blocks.
	void update_block(Block * b);

	// Singleton
	static Block_manager *instance();
	static void instance_initialize(FtlParent *ftl);
	static Block_manager *inst;

	void cost_insert(Block *b);
	void print_cost_status();
private:
	void get_page_block(Address &address, Event &event);
	static bool block_comparitor_simple (Block const *x,Block const *y);

	FtlParent *ftl;

	ulong data_active;
	ulong log_active;
	ulong logseq_active;

	ulong max_log_blocks;
	ulong max_blocks;

	ulong max_map_pages;
	ulong map_space_capacity;

	// Cost/Benefit priority queue.
	typedef boost::multi_index_container<
			Block*,
			boost::multi_index::indexed_by<
				boost::multi_index::random_access<>,
				boost::multi_index::ordered_non_unique<BOOST_MULTI_INDEX_MEMBER(Block,uint,pages_invalid) >
		  >
		> active_set;

	typedef active_set::nth_index<0>::type ActiveBySeq;
	typedef active_set::nth_index<1>::type ActiveByCost;
	active_set active_cost;
	// Usual block lists
	vector<Block*> active_list;
	vector<Block*> free_list;
	vector<Block*> invalid_list;
	// Counter for returning the next free page.
	ulong directoryCurrentPage;
	// Address on the current cached page in SRAM.
	ulong directoryCachedPage;
	ulong simpleCurrentFree;
	// Counter for handling periodic sort of active_list
	uint num_insert_events;
	uint current_writing_block;
	bool inited;
	bool out_of_blocks;
};*/

} /* end namespace ssd */

#endif
