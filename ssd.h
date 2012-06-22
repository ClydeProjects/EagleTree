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

#ifndef _SSD_H
#define _SSD_H

namespace ssd {

/* define exit codes for errors */
#define MEM_ERR -1
#define FILE_ERR -2

/* Uncomment to disable asserts for production */
#define NDEBUG


/* some obvious typedefs for laziness */
typedef unsigned int uint;
typedef unsigned long ulong;


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
extern const double BUS_CTRL_DELAY;
extern const double BUS_DATA_DELAY;
extern const uint BUS_MAX_CONNECT;
extern const double BUS_CHANNEL_FREE_FLAG;
extern const uint BUS_TABLE_SIZE;
/* extern const uint BUS_CHANNELS = 4; same as # of Packages, defined by SSD_SIZE */

/* Ssd class:
 * 	number of Packages per Ssd (size) */
extern const uint SSD_SIZE;

/* Package class:
 * 	number of Dies per Package (size) */
extern const uint PACKAGE_SIZE;

/* Die class:
 * 	number of Planes per Die (size) */
extern const uint DIE_SIZE;

/* Plane class:
 * 	number of Blocks per Plane (size)
 * 	delay for reading from plane register
 * 	delay for writing to plane register
 * 	delay for merging is based on read, write, reg_read, reg_write 
 * 		and does not need to be explicitly defined */
extern const uint PLANE_SIZE;
extern const double PLANE_REG_READ_DELAY;
extern const double PLANE_REG_WRITE_DELAY;

/* Block class:
 * 	number of Pages per Block (size)
 * 	number of erases in lifetime of block
 * 	delay for erasing block */
extern const uint BLOCK_SIZE;
extern const uint BLOCK_ERASES;
extern const double BLOCK_ERASE_DELAY;

/* Page class:
 * 	delay for Page reads
 * 	delay for Page writes */
extern const double PAGE_READ_DELAY;
extern const double PAGE_WRITE_DELAY;
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

extern const uint NUMBER_OF_ADDRESSABLE_BLOCKS;

/* RAISSDs: Number of physical SSDs */
extern const uint RAID_NUMBER_OF_PHYSICAL_SSDS;

/*
 * Memory area to support pages with data.
 */
extern void *page_data;
extern void *global_buffer;

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
enum event_type{READ, READ_COMMAND, READ_TRANSFER, WRITE, ERASE, MERGE, TRIM};

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
class Garbage_Collector;
class Wear_Leveler;
class Block_manager;
class Block_manager_parent;
class Block_manager_parallel;
class Block_manager_parallel_wearwolf;
class Block_manager_parallel_hot_cold_seperation;
class Page_Hotness_Measurer;
class IOScheduler;
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
	//Event(enum event_type type, ulong logical_address, uint size, double start_time, uint application_io_id);
	~Event(void);
	void consolidate_metaevent(Event &list);
	ulong get_logical_address(void) const;
	const Address &get_address(void) const;
	const Address &get_merge_address(void) const;
	const Address &get_log_address(void) const;
	const Address &get_replace_address(void) const;
	uint get_size(void) const;
	enum event_type get_event_type(void) const;
	double get_start_time(void) const;
	double get_time_taken(void) const;
	uint get_application_io_id(void) const;
	double get_bus_wait_time(void) const;
	bool get_noop(void) const;
	uint get_id(void) const;
	Event *get_next(void) const;
	void set_address(const Address &address);
	void set_merge_address(const Address &address);
	void set_log_address(const Address &address);
	void set_replace_address(const Address &address);
	void set_next(Event &next);
	void set_start_time(double start_time);
	void set_payload(void *payload);
	void set_event_type(const enum event_type &type);
	void set_noop(bool value);
	void set_application_io_id(uint application_io_id);
	void set_garbage_collection_op(bool value);
	void set_mapping_op(bool value);
	bool is_garbage_collection_op() const;
	bool is_mapping_op() const;
	void *get_payload(void) const;
	double incr_bus_wait_time(double time);
	double incr_time_taken(double time_incr);
	void print(FILE *stream = stdout) const;
private:
	double start_time;
	double time_taken;
	double bus_wait_time;
	enum event_type type;

	ulong logical_address;
	Address address;
	Address merge_address;
	Address log_address;
	Address replace_address;
	uint size;
	void *payload;
	Event *next;
	bool noop;

	bool garbage_collection_op;
	bool mapping_op;

	// an ID for a single IO to the chip. This is not actually used for any logical purpose
	static uint id_generator;
	uint id;

	// an ID to manage dependencies in the scheduler.
	uint application_io_id;
	static uint application_io_id_generator;
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
	//std::vector<lock_times> timings;

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
	enum status _merge(Event &event);
	const Die &get_parent(void) const;
	double get_last_erase_time(const Address &address) const;
	ulong get_erases_remaining(const Address &address) const;
	void get_least_worn(Address &address) const;
	uint get_size(void) const;
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	ssd::uint get_num_free(const Address &address) const;
	ssd::uint get_num_valid(const Address &address) const;
	ssd::uint get_num_invalid(const Address &address) const;
	Block *get_block_pointer(const Address & address);
	Block *getBlocks();
private:
	void update_wear_stats(void);
	enum status get_next_page(void);
	uint size;
	Block * const data;
	const Die &parent;
	uint least_worn;
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
	enum status merge(Event &event);
	enum status _merge(Event &event);
	const Package &get_parent(void) const;
	double get_last_erase_time(const Address &address) const;
	ulong get_erases_remaining(const Address &address) const;
	void get_least_worn(Address &address) const;
	double get_currently_executing_io_finish_time();
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	ssd::uint get_num_free(const Address &address) const;
	ssd::uint get_num_valid(const Address &address) const;
	ssd::uint get_num_invalid(const Address &address) const;
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
	enum status merge(Event &event);
	const Ssd &get_parent(void) const;
	double get_last_erase_time (const Address &address) const;
	ulong get_erases_remaining (const Address &address) const;
	void get_least_worn (Address &address) const;
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	ssd::uint get_num_free(const Address &address) const;
	ssd::uint get_num_valid(const Address &address) const;
	ssd::uint get_num_invalid(const Address &address) const;
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

/* place-holder definitions for GC, WL, FTL, RAM, Controller 
 * please make sure to keep this order when you replace with your definitions */
class Garbage_collector 
{
public:
	Garbage_collector(FtlParent &ftl);
	~Garbage_collector(void);
private:
	void clean(Address &address);
};

class Wear_leveler 
{
public:
	Wear_leveler(FtlParent &FTL);
	~Wear_leveler(void);
	enum status insert(const Address &address);
};

// Interface of page hotness measurer
class Page_Hotness_Measurer {
public:
	//virtual Page_Hotness_Measurer() = 0;
	//virtual ~Page_Hotness_Measurer(void) = 0;
	virtual void register_event(Event const& event) = 0; // Inform hotness measurer about a read or write event
	virtual enum write_hotness get_write_hotness(unsigned long page_address) const = 0; // Return write hotness of a given page address
	virtual enum read_hotness get_read_hotness(unsigned long page_address) const = 0; // Return read hotness of a given page address
	virtual Address get_die_with_least_WC(enum read_hotness rh) const = 0; // Return address of die with leads WC data (with chosen read hotness)
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
	std::map<ulong, uint> write_current_count;
	std::vector<double> write_moving_average;
	std::map<ulong, uint> read_current_count;
	std::vector<double> read_moving_average;
	ssd::uint current_interval;
	double average_write_hotness;
	double average_read_hotness;
	std::vector<std::vector<uint> > num_wcrh_pages_per_die;
	std::vector<std::vector<uint> > num_wcrc_pages_per_die;
	std::vector<std::vector<double> > average_reads_per_die;
	std::vector<std::vector<uint> > current_reads_per_die;
	uint writes_counter;
	uint reads_counter;
};

// BloomFilter hotness
//typedef std::vector<bool> BloomFilter;
typedef std::vector< bloom_filter > hot_bloom_filter;
typedef std::vector< std::vector<unsigned int> > lun_counters;

class Die_Stats {
public:
	Die_Stats(bloom_parameters bloomfilter_parameters)
	:	live_pages(0),
	 	reads(0),
	 	reads_targeting_wc_pages(0),
	 	reads_targeting_wc_pages_previous_window(0),
	 	writes(0),
	 	unique_wh_encountered(0),
	 	unique_wh_encountered_previous_window(0),
	 	wh_counted_already(bloomfilter_parameters)
	{}

	// WC = live pages - WH
	inline unsigned int wc_pages() {
		return unique_wh_encountered_previous_window == 0 ? live_pages - unique_wh_encountered : live_pages - unique_wh_encountered_previous_window;
	}

	unsigned int live_pages;

	unsigned int reads;
	unsigned int reads_targeting_wc_pages;
	unsigned int reads_targeting_wc_pages_previous_window;

	unsigned int writes;
	unsigned int unique_wh_encountered;
	unsigned int unique_wh_encountered_previous_window;
	bloom_filter wh_counted_already;

	//unsigned int read_counter;
	//unsigned int read_hot_counter;
	//unsigned int last_window_hot_count;
	//bloom_filter counted_already;
};

class BloomFilter_Page_Hotness_Measurer : public Page_Hotness_Measurer {
public:
	BloomFilter_Page_Hotness_Measurer(unsigned int num_bloom_filters = 4, unsigned int bloom_filter_size = 2048, unsigned int decay_time = 512);
	~BloomFilter_Page_Hotness_Measurer(void);
	void register_event(Event const& event);
	enum write_hotness get_write_hotness(unsigned long page_address) const;
	enum read_hotness get_read_hotness(unsigned long page_address) const;
	Address get_die_with_least_WC(enum read_hotness rh) const;
private:
	double get_hot_data_index(hot_bloom_filter const& filter, unsigned long page_address) const;

	// Parameters
	unsigned int V, M, /*K,*/ T, hotness_threshold, read_counter_window_size, write_counter_window_size;

	// Bookkeeping variables
	unsigned int oldest_BF;
	unsigned int BF_read_pos, BF_write_pos;
	unsigned int read_counter, write_counter;
	hot_bloom_filter read_bloom;
	hot_bloom_filter write_bloom;
	std::vector< std::vector<Die_Stats> > package_die_stats;

	//	std::vector< std::map<int, bool> > ReadErrorFreeCounter;
//	std::vector< std::map<int, bool> > WriteErrorFreeCounter;
};

class Block_manager_parent {
public:
	Block_manager_parent(Ssd& ssd, FtlParent& ftl);
	~Block_manager_parent();
	virtual void register_write_outcome(Event const& event, enum status status);
	virtual void register_write_arrival(Event const& event);
	virtual void register_read_outcome(Event const& event, enum status status);
	virtual void register_erase_outcome(Event const& event, enum status status);
	virtual Address choose_write_location(Event const& event) const = 0;
	virtual bool can_write(Event const& write) const;
protected:
	virtual void Garbage_Collect(uint package_id, uint die_id, double start_time);
	void perform_emergency_garbage_collection(double start_time);
	virtual void check_if_should_trigger_more_GC(double start_time);
	virtual void Wear_Level(Event const& event);
	virtual Address find_free_unused_block(uint package_id, uint die_id);
	Address find_free_unused_block();
	virtual Address get_free_die_with_shortest_IO_queue() const;
	Ssd& ssd;
	FtlParent& ftl;

	std::vector<std::vector<Address> > free_block_pointers;
	std::vector<std::vector<std::vector<Address> > > free_blocks;
private:
	void migrate(Block const* const block, double start_time);
	void update_blocks_with_min_age(uint age);
	std::vector<std::vector<std::vector<Block*> > > blocks;
	std::vector<Block*> all_blocks;
	// WL structures
	uint max_age;
	std::set<Block*> blocks_with_min_age;
	std::queue<Block*> blocks_to_wl;
	uint num_free_pages;
	uint num_available_pages_for_new_writes;
	std::set<long> blocks_currently_undergoing_gc;
};

// A BM that assigns each write to the die with the shortest queue. No hot-cold seperation
class Block_manager_parallel : public Block_manager_parent {
public:
	Block_manager_parallel(Ssd& ssd, FtlParent& ftl);
	~Block_manager_parallel();
	virtual void register_write_outcome(Event const& event, enum status status);
	virtual void register_erase_outcome(Event const& event, enum status status);
	virtual Address choose_write_location(Event const& event) const;
	virtual bool can_write(Event const& write) const;
private:
	bool has_free_pages(uint package_id, uint die_id) const;
};

class Block_manager_parallel_wearwolf : public Block_manager_parent {
public:
	Block_manager_parallel_wearwolf(Ssd& ssd, FtlParent& ftl);
	~Block_manager_parallel_wearwolf();
	virtual void register_write_outcome(Event const& event, enum status status);
	virtual void register_read_outcome(Event const& event, enum status status);
	virtual void register_erase_outcome(Event const& event, enum status status);
	virtual Address choose_write_location(Event const& event) const;
	virtual bool can_write(Event const& write) const;
protected:
	virtual void check_if_should_trigger_more_GC(double start_time);
private:
	bool pointer_can_be_written_to(Address pointer) const;
	bool at_least_one_available_write_hot_pointer() const;
	void handle_cold_pointer_out_of_space(enum read_hotness rh, double start_time);
	Simple_Page_Hotness_Measurer page_hotness_measurer;
	Address wcrh_pointer;
	Address wcrc_pointer;

};

// A BM that assigns each write to the die with the shortest queue, as well as hot-cold seperation
class Block_manager_parallel_hot_cold_seperation : public Block_manager_parent {
public:
	Block_manager_parallel_hot_cold_seperation(Ssd& ssd, FtlParent& ftl);
	~Block_manager_parallel_hot_cold_seperation();
	virtual void register_write_outcome(Event const& event, enum status status);
	virtual void register_read_outcome(Event const& event, enum status status);
	virtual void register_erase_outcome(Event const& event, enum status status);
	virtual Address choose_write_location(Event const& event) const;
	virtual bool can_write(Event const& write) const;
protected:
	virtual void check_if_should_trigger_more_GC(double start_time);
private:

	bool pointer_can_be_written_to(Address pointer) const;
	bool at_least_one_available_write_hot_pointer() const;
	void handle_cold_pointer_out_of_space(double start_time);
	Simple_Page_Hotness_Measurer page_hotness_measurer;
	Address cold_pointer;
};

class Block_manager
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
	std::vector<Block*> active_list;
	std::vector<Block*> free_list;
	std::vector<Block*> invalid_list;
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
};

class IOScheduler {
public:
	void schedule_dependent_events(std::queue<Event>& events);
	void schedule_independent_event(Event& events);
	void finish(double start_time);
	static IOScheduler *instance();
	static void instance_initialize(Ssd& ssd, FtlParent& ftl);
private:
	IOScheduler(Ssd& ssd, FtlParent& ftl);
	~IOScheduler();
	enum status execute_next(Event& event);
	std::vector<Event> gather_current_waiting_ios();
	void execute_current_waiting_ios();
	void execute_next_batch(std::vector<Event>& events);
	void handle_writes(std::vector<Event>& events);

	double in_how_long_can_this_event_be_scheduled(Event const& event) const;
	bool can_schedule_on_die(Event const& event) const;
	void handle_finished_event(Event const&event, enum status outcome);

	void eliminate_conflict_with_any_incoming_gc(Event const&event);
	void adjust_conflict_elimination_structures(Event const&event);

	std::vector<Event> io_schedule;
	std::map<uint, std::deque<Event> > dependencies;

	static IOScheduler *inst;
	Ssd& ssd;
	FtlParent& ftl;
	Block_manager_parallel_hot_cold_seperation bm;

	std::map<uint, uint> LBA_to_dependencies;  // maps LBAs to dependency codes of GC operations.
};

class FtlParent
{
public:
	FtlParent(Controller &controller);

	virtual ~FtlParent () {};
	virtual enum status read(Event &event) = 0;
	virtual enum status write(Event &event) = 0;
	virtual enum status trim(Event &event) = 0;
	virtual void cleanup_block(Event &event, Block *block);

	virtual void print_ftl_statistics();

	friend class Block_manager;
	friend class Block_manager_parallel;

	ulong get_erases_remaining(const Address &address) const;
	void get_least_worn(Address &address) const;
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	Block *get_block_pointer(const Address & address);
	Address resolve_logical_address(unsigned int logicalAddress);
	// TODO: this method should be abstract, but I am not making it so because
	// I dont't want to implement it in BAST and FAST yet
	virtual void register_write_completion(Event const& event, enum status result);
	virtual void register_read_completion(Event const& event, enum status result);
	virtual long get_logical_address(uint physical_address) const;
	virtual void set_replace_address(Event& event) const;
	virtual void set_read_address(Event& event) const;
protected:
	Controller &controller;
};

class FtlImpl_Page : public FtlParent
{
public:
	FtlImpl_Page(Controller &controller);
	~FtlImpl_Page();
	enum status read(Event &event);
	enum status write(Event &event);
	enum status trim(Event &event);
private:
	ulong currentPage;
	ulong numPagesActive;
	bool *trim_map;
	long *map;
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
	std::map<long, LogPageBlock*> log_map;

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

	std::map<long, LogPageBlock*> log_map;

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
	virtual enum status read(Event &event) = 0;
	virtual enum status write(Event &event) = 0;
	virtual enum status trim(Event &event) = 0;
protected:
	struct MPage {
		long vpn;
		long ppn;
		double create_ts;		//when its added to the CTM
		double modified_ts;		//when its modified within the CTM
		bool cached;

		MPage(long vpn);
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

	void consult_GTD(long dppn, Event &event);
	void reset_MPage(FtlImpl_DftlParent::MPage &mpage);

	void resolve_mapping(Event &event, bool isWrite);
	void update_translation_map(FtlImpl_DftlParent::MPage &mpage, long ppn);

	bool lookup_CMT(long dlpn, Event &event);

	long get_free_data_page(Event &event);
	long get_free_data_page(Event &event, bool insert_events);

	void evict_page_from_cache( Event & event);
	void evict_specific_page_from_cache(Event &event, long lba);

	long get_logical_address(uint physical_address) const;

	// Mapping information
	int addressSize;
	int addressPerPage;
	uint totalCMTentries;

	// Current storage
	long currentDataPage;
	long currentTranslationPage;

	std::queue<Event> current_dependent_events;

	std::vector<long> global_translation_directory;

	// Translation blocks, and mapping from logical translation pages to physical translation pages
	//std::vector<Address> translationBlocks;
	//std::map<ulong, Address> logicalToPhysicalTranslationPageMapping;
	//Address compute_logical_translation_page(long logical_page_address);
};

class FtlImpl_Dftl : public FtlImpl_DftlParent
{
public:
	FtlImpl_Dftl(Controller &controller);
	~FtlImpl_Dftl();
	enum status read(Event &event);
	enum status write(Event &event);
	enum status trim(Event &event);
	void cleanup_block(Event &event, Block *block);
	void print_ftl_statistics();
	void register_write_completion(Event const& event, enum status result);
	virtual void register_read_completion(Event const& event, enum status result);
	virtual void set_replace_address(Event& event) const;
	virtual void set_read_address(Event& event) const;
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

	std::queue<Block*> blockQueue;

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
	enum status event_arrive(Event *event);
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
	enum status issue(Event &event_list);
	void translate_address(Address &address);
	ssd::ulong get_erases_remaining(const Address &address) const;
	void get_least_worn(Address &address) const;
	double get_last_erase_time(const Address &address) const;
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	ssd::uint get_num_free(const Address &address) const;
	ssd::uint get_num_valid(const Address &address) const;
	ssd::uint get_num_invalid(const Address &address) const;
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
	double event_arrive(enum event_type type, ulong logical_address, uint size, double start_time);
	double event_arrive(enum event_type type, ulong logical_address, uint size, double start_time, void *buffer);
	void *get_result_buffer();
	friend class Controller;
	friend class IOScheduler;
	friend class Block_manager_parent;
	void print_statistics();
	void reset_statistics();
	void write_statistics(FILE *stream);
	void write_header(FILE *stream);
	const Controller &get_controller(void);

	void print_ftl_statistics();
	double ready_at(void);
	Package* getPackages();
private:
	enum status read(Event &event);
	enum status write(Event &event);
	enum status erase(Event &event);
	enum status merge(Event &event);
	enum status replace(Event &event);
	enum status merge_replacement_block(Event &event);
	ulong get_erases_remaining(const Address &address) const;
	void update_wear_stats(const Address &address);
	void get_least_worn(Address &address) const;
	double get_last_erase_time(const Address &address) const;	
	Package &get_data(void);
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	ssd::uint get_num_free(const Address &address) const;
	ssd::uint get_num_valid(const Address &address) const;
	ssd::uint get_num_invalid(const Address &address) const;
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
	// Singleton
	static VisualTracer *get_instance();
	static void init();

	void register_completed_event(Event const& event);
	void print();
private:
	static VisualTracer *inst;
	VisualTracer();
	~VisualTracer();
	void write(int package, int die, char symbol, int length);
	std::vector<std::vector<std::vector<char> > > trace;
};

} /* end namespace ssd */

#endif
