
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

extern bool ENABLE_WEAR_LEVELING;
extern int WEAR_LEVEL_THRESHOLD;
extern int MAX_ONGOING_WL_OPS;

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

class IOScheduler;

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
	inline int get_age_class() const 						{ return age_class; }
	inline bool is_garbage_collection_op() const 			{ return garbage_collection_op; }
	inline bool is_mapping_op() const 						{ return mapping_op; }
	inline void *get_payload() const 						{ return payload; }
	inline double incr_bus_wait_time(double time_incr) 		{ if(time_incr > 0.0) bus_wait_time += time_incr; return bus_wait_time; }
	inline double incr_os_wait_time(double time_incr) 		{ if(time_incr > 0.0) os_wait_time += time_incr; return os_wait_time; }
	inline double incr_execution_time(double time_incr) 	{ if(time_incr > 0.0) execution_time += time_incr; return execution_time; }
	inline double incr_accumulated_wait_time(double time_incr) 	{ if(time_incr > 0.0) accumulated_wait_time += time_incr; return accumulated_wait_time; }
	inline double get_overall_wait_time() const 				{ return accumulated_wait_time + bus_wait_time; }
	inline double get_latency() const 				{ return accumulated_wait_time + bus_wait_time + execution_time; }
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
	double get_second_last_erase_time(void) const;
	double get_modification_time(void) const;
	ulong get_erases_remaining(void) const;
	uint get_size(void) const;
	enum status get_next_page(Address &address) const;
	void invalidate_page(uint page);
	long get_physical_address(void) const;
	Block *get_pointer(void);
	block_type get_block_type(void) const;
	void set_block_type(block_type value);
	const Page *getPages() const { return data; }
	ulong get_age() const;
private:
	uint size;
	Page * const data;
	const Plane &parent;
	uint pages_valid;
	enum block_state state;
	ulong erases_remaining;
	double last_erase_time;
	double erase_before_last_erase_time;
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
	inline Block *getBlocks() { return data; }
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
	inline Plane *getPlanes() { return data; }
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
	inline Die *getDies() { return data; }
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

class Block_manager_parent {
public:
	Block_manager_parent(int classes = 1);
	virtual ~Block_manager_parent();

	void set_all(Ssd*, FtlParent*, IOScheduler*);

	virtual void register_write_outcome(Event const& event, enum status status);
	virtual void register_read_command_outcome(Event const& event, enum status status);
	virtual void register_read_transfer_outcome(Event const& event, enum status status);
	virtual void register_erase_outcome(Event const& event, enum status status);
	virtual void register_register_cleared();
	virtual Address choose_write_address(Event const& write);
	Address choose_flexible_read_address(Flexible_Read_Event* fr);
	virtual void register_write_arrival(Event const& write);
	virtual void trim(Event const& write);
	double in_how_long_can_this_event_be_scheduled(Address const& die_address, double current_time) const;
	vector<deque<Event*> > migrate(Event * gc_event);
	bool Copy_backs_in_progress(Address const& address);
	bool can_schedule_on_die(Address const& address, event_type type, uint app_io_id) const;
	bool is_die_register_busy(Address const& addr) const;
	void register_trim_making_gc_redundant();
	Address choose_copbyback_address(Event const& write);
protected:
	virtual Address choose_best_address(Event const& write) = 0;
	virtual Address choose_any_address(Event const& write) = 0;


	virtual void check_if_should_trigger_more_GC(double start_time);
	void increment_pointer(Address& pointer);
	bool can_schedule_write_immediately(Address const& prospective_dest, double current_time);
	bool can_write(Event const& write) const;

	void schedule_gc(double time, int package_id, int die_id, int block, int klass);
	vector<long> get_relevant_gc_candidates(int package_id, int die_id, int klass) const;

	Address find_free_unused_block(uint package_id, uint die_id, enum age age, double time);
	Address find_free_unused_block(uint package_id, uint die_id, double time);
	Address find_free_unused_block(uint package_id, double time);
	Address find_free_unused_block(double time);
	Address find_free_unused_block(enum age age, double time);

	void return_unfilled_block(Address block_address);

	pair<bool, pair<int, int> > get_free_block_pointer_with_shortest_IO_queue(vector<vector<Address> > const& dies) const;
	Address get_free_block_pointer_with_shortest_IO_queue();

	uint how_many_gc_operations_are_scheduled() const;

	inline bool has_free_pages(Address const& address) const { return address.valid == PAGE && address.page < BLOCK_SIZE; }

	Ssd* ssd;
	FtlParent* ftl;
	IOScheduler *scheduler;
	vector<vector<Address> > free_block_pointers;

	map<long, uint> page_copy_back_count; // Pages that have experienced a copy-back, mapped to a count of the number of copy-backs
private:
	Address find_free_unused_block(uint package_id, uint die_id, uint age_class, double time);
	Block* choose_gc_victim(vector<long> candidates) const;
	void update_blocks_with_min_age(uint age);
	uint sort_into_age_class(Address const& address) const;
	void issue_erase(Address a, double time);
	void remove_as_gc_candidate(Address const& phys_address);
	void Wear_Level(Event const& event);
	int get_num_free_blocks(int package, int die);
	bool copy_back_allowed_on(long logical_address);
	Address reserve_page_on(uint package, uint die, double time);
	void register_copy_back_operation_on(uint logical_address);
	void register_ECC_check_on(uint logical_address);

	bool schedule_queued_erase(Address location);
	double get_min_age() const;
	double get_normalised_age(uint age) const;
	void find_wl_candidates(double current_time);
	vector<vector<vector<vector<Address> > > > free_blocks;  // package -> die -> class -> list of such free blocks
	vector<Block*> all_blocks;
	// WL structures
	uint max_age;
	int num_age_classes;
	set<Block*> blocks_with_min_age;
	uint num_free_pages;
	uint num_available_pages_for_new_writes;
	map<int, int> blocks_being_garbage_collected;   // maps block physical address to the number of pages that still need to be migrated
	vector<vector<vector<set<long> > > > gc_candidates;  // each age class has a vector of candidates for GC
	vector<vector<uint> > num_blocks_being_garbaged_collected_per_LUN;
	Random_Order_Iterator order_randomiser;

	set<Block*> blocks_to_wl;
	double average_erase_cycle_time;
	MTRand_int32 random_number_generator;
	set<Block*> blocks_being_wl;
	uint num_erases_up_to_date;

	pair<bool, pair<int, int> > last_get_free_block_pointer_with_shortest_IO_queue_result;
	bool IO_has_completed_since_last_shortest_queue_search;

	vector<queue<Event*> > erase_queue;
	vector<int> num_erases_scheduled_per_package;
};

// A BM that assigns each write to the die with the shortest queue. No hot-cold seperation
class Block_manager_parallel : public Block_manager_parent {
public:
	Block_manager_parallel();
	~Block_manager_parallel() {}
	void register_write_outcome(Event const& event, enum status status);
	void register_erase_outcome(Event const& event, enum status status);
protected:
	Address choose_best_address(Event const& write);
	Address choose_any_address(Event const& write);
};

// A simple BM that assigns writes sequentially to dies in a round-robin fashion. No hot-cold separation or anything else intelligent
class Block_manager_roundrobin : public Block_manager_parent {
public:
	Block_manager_roundrobin(bool channel_alternation = true);
	~Block_manager_roundrobin();
	void register_write_outcome(Event const& event, enum status status);
	void register_erase_outcome(Event const& event, enum status status);
protected:
	Address choose_best_address(Event const& write);
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
	void register_erase_outcome(Event const& event, enum status status);
protected:
	Address choose_best_address(Event const& write);
	virtual Address choose_any_address(Event const& write);
	void check_if_should_trigger_more_GC(double start_time);
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
	virtual void register_erase_outcome(Event const& event, enum status status);
protected:
	virtual void check_if_should_trigger_more_GC(double start_time);
	virtual Address choose_best_address(Event const& write);
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
	virtual void sequential_event_metadata_removed(long key) = 0;
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

class Wearwolf_Locality : public Block_manager_parallel, public Sequential_Pattern_Detector_Listener {
public:
	Wearwolf_Locality();
	~Wearwolf_Locality();
	void register_write_arrival(Event const& write);
	void register_write_outcome(Event const& event, enum status status);
	void register_erase_outcome(Event const& event, enum status status);
	void sequential_event_metadata_removed(long key);
protected:
	Address choose_best_address(Event const& write);
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
	int num_hits;
	int num_misses;
};

class IOScheduler {
public:
	IOScheduler();
	~IOScheduler();
	//void schedule_dependent_events_queue(deque<deque<Event*> > events);

	void set_all(Ssd*, FtlParent*, Block_manager_parent*);

	void schedule_events_queue(deque<Event*> events);
	void schedule_event(Event* events);
	bool is_empty();
	void finish_all_events_until_this_time(double time);
	void execute_soonest_events();
	//static IOScheduler *instance();
	//static void instance_initialize(Ssd& ssd, FtlParent& ftl);
	void print_stats();
private:
	void setup_structures(deque<Event*> events);
	enum status execute_next(Event* event);
	void execute_current_waiting_ios();
	vector<Event*> collect_soonest_events();
	void handle_event(Event* event);
	void handle_write(Event* event);
	void handle_flexible_read(Event* event);
	void handle(vector<Event*>& events);
	void transform_copyback(Event* event);
	void handle_finished_event(Event *event, enum status outcome);
	void remove_redundant_events(Event* new_event);
	bool should_event_be_scheduled(Event* event);
	void init_event(Event* event);
	void handle_noop_events(vector<Event*>& events);

	bool remove_event_from_current_events(Event* event);

	void manage_operation_completion(Event* event);

	void push_into_current_events(Event* event);

	double get_soonest_event_time(vector<Event*> const& events) const;

	vector<Event*> future_events;
	//vector<Event*> current_events;
	map<long, vector<Event*> > current_events;
	map<uint, deque<Event*> > dependencies;

	//static IOScheduler *inst;
	Ssd* ssd;
	FtlParent* ftl;
	Block_manager_parent* bm;

	//map<uint, uint> LBA_to_dependencies;  // maps LBAs to dependency codes of GC operations. to be removed

	map<uint, uint> dependency_code_to_LBA;
	map<uint, event_type> dependency_code_to_type;
	map<uint, uint> LBA_currently_executing;

	map<uint, queue<uint> > op_code_to_dependent_op_codes;

	void update_current_events();
	long get_current_events_size();
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
	enum status replace(Event &event);
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
	void register_completed_event(Event const& event);
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

class Thread
{
public:
	Thread() : finished(false), time(1), threads_to_start_when_this_thread_finishes(), num_ios_finished(0), experiment_thread(false), os(NULL), statistics_gatherer(new StatisticsGatherer()) {}
	virtual ~Thread();
	deque<Event*> run();
	inline bool is_finished() { return finished; }
	inline void set_time(double current_time) { time = current_time; }
	inline double get_time() { return time; }
	inline void add_follow_up_thread(Thread* thread) { threads_to_start_when_this_thread_finishes.push_back(thread); }
	inline vector<Thread*>& get_follow_up_threads() { return threads_to_start_when_this_thread_finishes; }
	virtual void print_thread_stats();
	deque<Event*> register_event_completion(Event* event);
	inline void set_experiment_thread(bool val) { experiment_thread = val; }
	inline bool is_experiment_thread() { return experiment_thread; }
	void set_os(OperatingSystem*  op_sys);
	StatisticsGatherer* get_statistics_gatherer() { return statistics_gatherer; }
	void set_statistics_gatherer(StatisticsGatherer* new_statistics_gatherer) {
		if (statistics_gatherer != NULL) delete statistics_gatherer;
		statistics_gatherer = new_statistics_gatherer;
	}
protected:
	virtual Event* issue_next_io() = 0;
	virtual void handle_event_completion(Event* event) = 0;
	bool finished;
	double time;
	vector<Thread*> threads_to_start_when_this_thread_finishes;
	OperatingSystem* os;

	void submit(Event* event);
private:
	ulong num_ios_finished;
	bool experiment_thread;
	StatisticsGatherer* statistics_gatherer;
	deque<Event*> submitted_events;
};


class IO_Pattern_Generator
{
public:
	IO_Pattern_Generator(long min_LBA, long max_LBA) : min_LBA(min_LBA), max_LBA(max_LBA) {};
	virtual ~IO_Pattern_Generator() {};
	virtual int next() = 0;
	const long min_LBA, max_LBA;
};

class Random_IO_Pattern_Generator : public IO_Pattern_Generator
{
public:
	Random_IO_Pattern_Generator(long min_LBA, long max_LBA, ulong seed) : IO_Pattern_Generator(min_LBA, max_LBA), random_number_generator(seed) {};
	~Random_IO_Pattern_Generator() {};
	int next() { return min_LBA + random_number_generator() % (max_LBA - min_LBA + 1); };
private:
	MTRand_int32 random_number_generator;
};

class Sequential_IO_Pattern_Generator : public IO_Pattern_Generator
{
public:
	Sequential_IO_Pattern_Generator(long min_LBA, long max_LBA) : IO_Pattern_Generator(min_LBA, max_LBA), counter(min_LBA - 1) {};
	~Sequential_IO_Pattern_Generator() {};
	int next() { return counter == max_LBA ? counter = min_LBA - 1: ++counter; };
private:
	long counter;
};

class Simple_Thread : public Thread
{
public:
	Simple_Thread(IO_Pattern_Generator* generator, int MAX_IOS, event_type type);
	virtual Event* issue_next_io();
	virtual void handle_event_completion(Event* event);
	inline void set_num_ios(ulong num_ios) { number_of_times_to_repeat = num_ios; }
private:
	long number_of_times_to_repeat;
	event_type type;
	int num_ongoing_IOs;
	const int MAX_IOS;
	IO_Pattern_Generator* io_gen;
};

class Synchronous_Random_Writer : public Simple_Thread
{
public:
	Synchronous_Random_Writer(long min_LBA, long max_LBA, ulong randseed)
		: Simple_Thread(new Random_IO_Pattern_Generator(min_LBA, max_LBA, randseed), 1, WRITE) {}
};

class Synchronous_Random_Reader : public Simple_Thread
{
public:
	Synchronous_Random_Reader(long min_LBA, long max_LBA, ulong randseed )
		: Simple_Thread(new Random_IO_Pattern_Generator(min_LBA, max_LBA, randseed), 1, READ) {}
};

class Asynchronous_Random_Writer : public Simple_Thread
{
public:
	Asynchronous_Random_Writer(long min_LBA, long max_LBA, ulong randseed)
		: Simple_Thread(new Random_IO_Pattern_Generator(min_LBA, max_LBA, randseed), std::numeric_limits<int>::max(), WRITE) {}
};

class Synchronous_Sequential_Writer : public Simple_Thread
{
public:
	Synchronous_Sequential_Writer(long min_LBA, long max_LBA )
		: Simple_Thread(new Sequential_IO_Pattern_Generator(min_LBA, max_LBA), 1, WRITE) {}
};

class Asynchronous_Sequential_Writer : public Simple_Thread
{
public:
	Asynchronous_Sequential_Writer(long min_LBA, long max_LBA)
		: Simple_Thread(new Sequential_IO_Pattern_Generator(min_LBA, max_LBA), std::numeric_limits<int>::max(), WRITE) {
	}
};

class Synchronous_Sequential_Reader : public Simple_Thread
{
public:
	Synchronous_Sequential_Reader(long min_LBA, long max_LBA )
		: Simple_Thread(new Sequential_IO_Pattern_Generator(min_LBA, max_LBA), 1, READ) {}
};

class Asynchronous_Sequential_Reader : public Simple_Thread
{
public:
	Asynchronous_Sequential_Reader(long min_LBA, long max_LBA )
		: Simple_Thread(new Sequential_IO_Pattern_Generator(min_LBA, max_LBA), std::numeric_limits<int>::max(), READ) {}
};

class Asynchronous_Random_Thread_Reader_Writer : public Thread
{
public:
	Asynchronous_Random_Thread_Reader_Writer(long min_LBA, long max_LAB, int number_of_times_to_repeat, ulong randseed = 0);
	Event* issue_next_io();
	void handle_event_completion(Event* event) {};
private:
	long min_LBA, max_LBA;
	int number_of_times_to_repeat;
	MTRand_int32 random_number_generator;
};

class Collision_Free_Asynchronous_Random_Thread : public Thread
{
public:
	Collision_Free_Asynchronous_Random_Thread(long min_LBA, long max_LAB, int number_of_times_to_repeat, ulong randseed = 0, event_type type = WRITE);
	Event* issue_next_io();
	void handle_event_completion(Event* event);
private:
	long min_LBA, max_LBA;
	int number_of_times_to_repeat;
	MTRand_int32 random_number_generator;
	event_type type;
	set<long> logical_addresses_submitted;
};

// assuming the relation is made of contigouse pages
// RAM_available is the number of pages that fit into RAM
class External_Sort : public Thread
{
public:
	External_Sort(long relation_min_LBA, long relation_max_LBA, long RAM_available,
			long free_space_min_LBA, long free_space_max_LBA);
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

// Simulates the IO pattern of a grace hash join between two relations
class Grace_Hash_Join : public Thread
{
public:
	Grace_Hash_Join(long relation_A_min_LBA,        long relation_A_max_LBA,
					long relation_B_min_LBA,        long relation_B_max_LBA,
					long free_space_min_LBA,        long free_space_max_LBA,
					bool use_flexible_reads = true, bool use_tagging  = true,
					long rows_per_page      = 32,    int ranseed = 72);
	Event* issue_next_io();

	void handle_event_completion(Event* event);
	void static initialize_counter() { printf("grace_counter: %d\n", grace_counter); grace_counter = 0; };
	int get_counter() { return grace_counter; };
private:
	static int grace_counter;
	void execute_build_phase();

	void execute_probe_phase(Event* finished_event = NULL);
	void setup_probe_run();
	void read_smaller_bucket();
	void trim_smaller_bucket();
	void read_next_in_larger_bucket(Event* finished_event);

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
	enum {BUILD, PROBE, DONE} phase;
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

// This is a file manager that writes one file at a time sequentially
// files might be fragmented across logical space
// files have a random size determined by the file manager
// fragmentation will eventually happen
class File_Manager : public Thread
{
public:
	File_Manager(long min_LBA, long max_LBA, uint num_files_to_write, long max_file_size, ulong randseed = 0);
	~File_Manager();
	Event* issue_next_io();
	void handle_event_completion(Event* event);
	//virtual void print_thread_stats();
	static void reset_id_generator();
private:
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
	set<long> addresses_to_trim;
	const long max_file_size;
	//Throughput_Moderator throughout_moderator;
};

/*struct os_event {
	int thread_id;
	Event* pending_event;
	os_event(int thread_id, Event* event) : thread_id(thread_id), pending_event(event) {}
	os_event() : thread_id(UNDEFINED), pending_event(NULL) {}
};*/

class Flexible_Reader {
public:
	Flexible_Reader(FtlParent const& ftl, vector<Address_Range>);
	Event* read_next(double start_time);
	void register_read_commencement(Flexible_Read_Event*);
	inline bool is_finished() { return finished_counter == 0; }
	inline uint get_num_reads_left() { return finished_counter; }
	inline vector<vector<Address> > const& get_immediate_candidates()     { return immediate_candidates_physical_addresses; }
	inline vector<vector<long> >    const& get_immediate_candidates_lba() { return immediate_candidates_logical_addresses; }
	Address get_verified_candidate_address(uint package, uint die);
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
	inline Address get_verified_candidate_address(uint package, uint die) { return reader->get_verified_candidate_address(package, die); }
	inline void find_alternative_immediate_candidate(uint package, uint die) { reader->find_alternative_immediate_candidate(package, die); }
};

class Flexible_Reader_Thread : public Thread
{
public:
	Flexible_Reader_Thread(long min_LBA, long max_LAB, int number_of_times_to_repeat);
	Event* issue_next_io();
	void handle_event_completion(Event* event);
private:
	long min_LBA, max_LBA;
	bool ready_to_issue_next_write;
	int number_of_times_to_repeat;
	Flexible_Reader* flex_reader;
};

class OperatingSystem
{
public:
	OperatingSystem(vector<Thread*> threads);
	~OperatingSystem();
	void run();
	void register_event_completion(Event* event);
	void set_num_writes_to_stop_after(long num_writes);
	double get_experiment_runtime() const;
	Flexible_Reader* create_flexible_reader(vector<Address_Range>);
	void submit(Event* event);
private:
	int pick_unlocked_event_with_shortest_start_time();
	void dispatch_event(int thread_id);
	double get_event_minimal_completion_time(Event const*const event) const;
	bool is_LBA_locked(ulong lba);
	void update_thread_times(double time);
	Ssd * ssd;
	vector<Thread*> threads;

	struct Pending_Events {
		vector<deque<Event*> > event_queues;
		int num_pending_events;
		Pending_Events(int i);
		~Pending_Events();
		Event* peek(int i);
		Event* pop(int i);
		void append(int i, deque<Event*>);
		void push_back() { event_queues.push_back(deque<Event*>()); }
		inline int get_num_pending_events() { return num_pending_events; }
		inline int size() {return event_queues.size();};
	};
	Pending_Events events;

	map<long, queue<uint> > write_LBA_to_thread_id;
	map<long, queue<uint> > read_LBA_to_thread_id;
	map<long, queue<uint> > trim_LBA_to_thread_id;
	void lock(Event* event, int thread_id);
	void release_lock(Event*);

	// used to record which thread dispatched which IO
	map<long, long> app_id_to_thread_id_mapping;

	int currently_executing_ios_counter;
	double last_dispatched_event_minimal_finish_time;

	int currently_executing_trims_counter;

	set<uint> currently_executing_ios;
	long NUM_WRITES_TO_STOP_AFTER;
	long num_writes_completed;

	double time_of_experiment_start;
	double time_of_last_event_completed;

	int counter_for_user;
	int idle_time;
	double time;
};

class ExperimentResult {
public:
	ExperimentResult(string experiment_name, string data_folder, string variable_parameter_name);
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

class Experiment_Runner {
public:
	static double CPU_time_user();
	static double CPU_time_system();
	static double wall_clock_time();
	static string pretty_time(double time);
	static void draw_graph(int sizeX, int sizeY, string outputFile, string dataFilename, string title, string xAxisTitle, string yAxisTitle, string xAxisConf, string command);
	static void draw_graph_with_histograms(int sizeX, int sizeY, string outputFile, string dataFilename, string title, string xAxisTitle, string yAxisTitle, string xAxisConf, string command, vector<string> histogram_commands);
	static double calibrate_IO_submission_rate_queue_based(int highest_lba, int IO_limit, vector<Thread*> (*experiment)(int highest_lba, double IO_submission_rate));
	static double measure_throughput(int highest_lba, double IO_submission_rate, int IO_limit, vector<Thread*> (*experiment)(int highest_lba, double IO_submission_rate));
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
	static vector<ExperimentResult> random_writes_on_the_side_experiment(vector<Thread*> (*experiment)(int highest_lba, double IO_submission_rate), int write_threads_min, int write_threads_max, int write_threads_inc, string data_folder, string name, int IO_limit, double used_space, int random_writes_min_lba, int random_writes_max_lba);
	static ExperimentResult copyback_experiment(vector<Thread*> (*experiment)(int highest_lba, double IO_submission_rate), int used_space, int max_copybacks, string data_folder, string name, int IO_limit);
	static ExperimentResult copyback_map_experiment(vector<Thread*> (*experiment)(int highest_lba, double IO_submission_rate), int cb_map_min, int cb_map_max, int cb_map_inc, int used_space, string data_folder, string name, int IO_limit);

	static string graph_filename_prefix;

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
