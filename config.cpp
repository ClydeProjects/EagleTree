#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits>
using namespace std;
/* using namespace ssd; */
namespace ssd {

/* Define typedefs and error macros from ssd.h here instead of including
 * header file because we want to declare the global configuration variables
 * and set them in this file.  ssd.h declares the global configuration
 * variables as extern const, which would conflict this file's definitions.
 * This is not the best solution, but it is easy and it works. */

/* some obvious typedefs for laziness */
typedef unsigned int uint;
typedef unsigned long ulong;

/* define exit codes for errors */
#define MEM_ERR -1
#define FILE_ERR -2

/* Simulator configuration
 * All configuration variables are set by reading ssd.conf and referenced with
 * 	as "extern const" in ssd.h
 * Configuration variables are described below and are assigned default values
 * 	in case of config file error.  The values defined below are overwritten
 * 	when defined in the config file.
 * We do not want a class here because we want to use the configuration
 * 	variables in the same was as macros. */

int UNDEFINED = -1;
int INFINITE = std::numeric_limits<int>::max();

/* Ram class:
 * 	delay to read from and write to the RAM for 1 page of data */
double RAM_READ_DELAY = 0.00000001;
double RAM_WRITE_DELAY = 0.00000001;

// The amount of time in microseconds to transmit a command from the SSD controller to a chip
double BUS_CTRL_DELAY = 5;

// The amount of time in microseconds to transmit a page between the SSD controller and a chip
double BUS_DATA_DELAY = 100;

// Number of packages in the ssd
uint SSD_SIZE = 4;

// Number of dies in a package
uint PACKAGE_SIZE = 8;

// Number of planes in a die
// We currently do not support multiple planes per die
uint DIE_SIZE = 1;

// Number of blocks in a plane
uint PLANE_SIZE = 64;

// Number of pages in a block
uint BLOCK_SIZE = 16;

// Lifetime if a block in erases
uint BLOCK_ERASES = 1048675;

// Time for an erase operation
double BLOCK_ERASE_DELAY = 1000;

// Time for reading a flash page
double PAGE_READ_DELAY = 0.000001;

// Time for writing a flash page
double PAGE_WRITE_DELAY = 0.00001;

// The size of a page in kilobytes.
uint PAGE_SIZE = 4096;

// The IO scheduler used by the Operating System.
// There are currently two schedulers available
// 0 corresponds to a FIFO scheduler, which is similar to the noop IO scheduler in Linux
// 1 corresponds to a fair scheduler that scheduels IOs in a round robin manner from different threads. It is similar to the CFQ Linux scheduler
// You can create more schedulers by extending the OS_Scheduler class.
int OS_SCHEDULER = 0;

uint NUMBER_OF_ADDRESSABLE_BLOCKS = 0;

// Determines the aggresiveness of how the internal SSD scheduler schedules erases
// The idea is that erases are long and may delay other operations.
// Erases also often appear in bulks, for example if we trim a large file
// If set to true, then we use a queue of erases. If false, we schedule all erases immediately.
bool USE_ERASE_QUEUE = false;


/*
 * The SSD controller IO scheduler works by finding a LUN on which something can be scheduled soonest,
 * and then finding an operation that can be scheduled on that LUN according to a priority scheme.
 * The priority scheme is determined by by the SCHEDULING_SCHEME parameter.
 * 0 ->  FIFO: disregards event types and schedules everything fifo
 * 1 ->  Noop: schedules the next event in an arbitrary manner. This is fastest in terms of real execution time of the simulator.
 * 			   however, latency outliers may occur and be significant. This scheduler is typically used for calibration.
 * 2 ->  Smart: internal reads, external reads, copybacks, erases, external writes, internal writes
 */
int SCHEDULING_SCHEME = 2;

bool ENABLE_WEAR_LEVELING = false;
int WEAR_LEVEL_THRESHOLD = 100;
int MAX_ONGOING_WL_OPS = 1;
int MAX_CONCURRENT_GC_OPS = 1;

/*
 * Block manager controls how writes are allocated across the physical architecture of the device
 * 0 -> Shortest Queues - This is a simple FIFO block scheduler that assigns the next write to whichever package is free
 * 1 -> Shortest Queues with Hot/Cold data seperation -- This block manager uses two active blocks in each die.
 * 		One of these blocks is used for storing cold pages, and the other is used for storing hot pages.
 * 2 -> This block manager is currently broken
 * 3 -> Sequential Locality Exploiter - This block manager is like 0, but it is able to detect sequential writes
 * 		across the logical address space. After a certain threshold of such writes, defined by the variable SEQUENTIAL_LOCALITY_THRESHOLD,
 * 		it clusters pages from the same sequential write in the same flash blocks.
 * 4 -> Round Robin
 */
int BLOCK_MANAGER_ID = 3;

/*
 * The policy used to choose a garbage-collection victim
 * 0 -> Greedy - for each LUN, always picks the block with the least number of pages
 * 1 -> LRU -- for each LUN, always picks the block that was cleaned last
 */
int GARBAGE_COLLECTION_POLICY = 0;

// This parameter is special for block manager 3. If is the threshold governing when to start dedicating blocks
// exclusively for a given sequential write
int SEQUENTIAL_LOCALITY_THRESHOLD = 10;

/* This parameter is special for block manager 3. If defines how aggressively we allocate blocks for sequential write
 * 0 means just 1 block is used. 1 means 1 block per channel is allocated. 2 means 1 block per die is allocated.  */
uint LOCALITY_PARALLEL_DEGREE = 0;

// This determines how greedy the garbage-collection is.
// The number corresponds to the number of live pages per die before garbage-collection kicks in
// to clear more space in the die
int GREED_SCALE = 2;

/* FTL Design
 * 0 -> Page FTL
 * 1 -> DFTL
 * 2 -> FAST
 * 3 -> LSM FTL
 */
int FTL_DESIGN = 0;
bool IS_FTL_PAGE_MAPPING = 0;

/* Output level of detail:
 * 0 -> Nothing
 * 1 -> Semi-detailed
 * 2 -> Detailed
 */
int PRINT_LEVEL = 0;

int PRINT_FILE_MANAGER_INFO = false;

bool ENABLE_TAGGING = false;

// This determines how reads are scheduled.
// Recall that a read consists of two parts.
// In the first part, a command is sent to the SSD and a read takes place in the chip.
// In the second part, the page is transmitted from the chip to the controller.
// If false, this parameter makes the second part happen immediately after the first part
// If true, it allows deferring the second part. This allow us to use the channel for different things. In the meanwhile, the page is assumed to be stored in the die buffer.
bool ALLOW_DEFERRING_TRANSFERS = true;

// The fraction of the SSD that is addressable.
double OVER_PROVISIONING_FACTOR = 0.7;

/* Defines the max number of copy back operations on a page before ECC check is performed.
 * Set to zero to disable copy back GC operations */
uint MAX_REPEATED_COPY_BACKS_ALLOWED = 0;

/* Defines the max number of page addresses in map keeping track of each pages copy back count */
uint MAX_ITEMS_IN_COPY_BACK_MAP = 1024;

/* Defines the maximal length of the number of outstanding IOs that the OS can submit to the SSD  */
int MAX_SSD_QUEUE_SIZE = 32;

// These are internal deadlines for scheduling IOs inside the SSD. They are in microseconds.
int WRITE_DEADLINE = 10000000;
int READ_DEADLINE =  10000000;
int READ_TRANSFER_DEADLINE = 10000000;

// This is to be ignored for now
int PAGE_HOTNESS_MEASURER = 0;

// The amount of SRAM available to the FTL in bytes
int SRAM;

void load_entry(char *name, double value, uint line_number) {
	/* cheap implementation - go through all possibilities and match entry */
	if (!strcmp(name, "BUS_CTRL_DELAY"))
		BUS_CTRL_DELAY = value;
	else if (!strcmp(name, "BUS_DATA_DELAY"))
		BUS_DATA_DELAY = value;
	else if (!strcmp(name, "SSD_SIZE"))
		SSD_SIZE = (uint) value;
	else if (!strcmp(name, "PACKAGE_SIZE"))
		PACKAGE_SIZE = (uint) value;
	else if (!strcmp(name, "DIE_SIZE"))
		DIE_SIZE = (uint) value;
	else if (!strcmp(name, "PLANE_SIZE"))
		PLANE_SIZE = (uint) value;
	else if (!strcmp(name, "BLOCK_SIZE"))
		BLOCK_SIZE = (uint) value;
	else if (!strcmp(name, "BLOCK_ERASES"))
		BLOCK_ERASES = (uint) value;
	else if (!strcmp(name, "BLOCK_ERASE_DELAY"))
		BLOCK_ERASE_DELAY = value;
	else if (!strcmp(name, "PAGE_READ_DELAY"))
		PAGE_READ_DELAY = value;
	else if (!strcmp(name, "PAGE_WRITE_DELAY"))
		PAGE_WRITE_DELAY = value;
	else if (!strcmp(name, "PAGE_SIZE"))
		PAGE_SIZE = value;
	else if (!strcmp(name, "MAX_REPEATED_COPY_BACKS_ALLOWED"))
		MAX_REPEATED_COPY_BACKS_ALLOWED = value;
	else if (!strcmp(name, "MAX_ITEMS_IN_COPY_BACK_MAP"))
		MAX_ITEMS_IN_COPY_BACK_MAP = value;
	else if (!strcmp(name, "MAX_SSD_QUEUE_SIZE"))
		MAX_SSD_QUEUE_SIZE = value;
	else if (!strcmp(name, "OVER_PROVISIONING_FACTOR"))
		OVER_PROVISIONING_FACTOR = value;
	else if (!strcmp(name, "BLOCK_MANAGER_ID"))
		BLOCK_MANAGER_ID = value;
	else if (!strcmp(name, "GREED_SCALE"))
		GREED_SCALE = value;
	else if (!strcmp(name, "MAX_CONCURRENT_GC_OPS"))
		MAX_CONCURRENT_GC_OPS = value;
	else if (!strcmp(name, "OS_SCHEDULER"))
		OS_SCHEDULER = value;
	else if (!strcmp(name, "GREED_SCALE"))
		GREED_SCALE = value;
	else if (!strcmp(name, "ALLOW_DEFERRING_TRANSFERS"))
		ALLOW_DEFERRING_TRANSFERS = value;
	else if (!strcmp(name, "SCHEDULING_SCHEME"))
		SCHEDULING_SCHEME = value;
	else if (!strcmp(name, "WRITE_DEADLINE"))
		WRITE_DEADLINE = value;
	else if (!strcmp(name, "READ_DEADLINE"))
		READ_DEADLINE = value;
	else if (!strcmp(name, "ENABLE_WEAR_LEVELING"))
		ENABLE_WEAR_LEVELING = value;
	else if (!strcmp(name, "ENABLE_TAGGING"))
		ENABLE_TAGGING = value;
	else
		fprintf(stderr, "Config file parsing error on line %u:  %s   %f\n", line_number, name, value);
	return;
}

void set_small_SSD_config() {
	SSD_SIZE = 4;
	PACKAGE_SIZE = 2;
	DIE_SIZE = 1;
	PLANE_SIZE = 1024;
	BLOCK_SIZE = 128;

	PAGE_READ_DELAY = 5;
	PAGE_WRITE_DELAY = 20;
	BUS_CTRL_DELAY = 1;
	BUS_DATA_DELAY = 10;
	BLOCK_ERASE_DELAY = 60;

	MAX_SSD_QUEUE_SIZE = 32;
	MAX_REPEATED_COPY_BACKS_ALLOWED = 0;
	SCHEDULING_SCHEME = 0;

	USE_ERASE_QUEUE = false;
	ENABLE_WEAR_LEVELING = false;
	BLOCK_MANAGER_ID = 0;
	GARBAGE_COLLECTION_POLICY = 0;
	MAX_CONCURRENT_GC_OPS = PACKAGE_SIZE * SSD_SIZE;
	GREED_SCALE = 2;
	ALLOW_DEFERRING_TRANSFERS = true;
	OVER_PROVISIONING_FACTOR = 0.7;

	OS_SCHEDULER = 0;

	FTL_DESIGN = 0;

	READ_TRANSFER_DEADLINE = PAGE_READ_DELAY + 1;// PAGE_READ_DELAY + 1;
}

void set_big_SSD_config() {
	SSD_SIZE = 8;
	PACKAGE_SIZE = 8;
	DIE_SIZE = 1;
	PLANE_SIZE = 1024;
	BLOCK_SIZE = 128;

	PAGE_READ_DELAY = 115 ;
	PAGE_WRITE_DELAY = 1600 ;
	BUS_CTRL_DELAY = 5 ;
	BUS_DATA_DELAY = 350 ;
	BLOCK_ERASE_DELAY = 3000 ;

	MAX_SSD_QUEUE_SIZE = 64;
	MAX_REPEATED_COPY_BACKS_ALLOWED = 0;
	SCHEDULING_SCHEME = 0;  // FIFO

	USE_ERASE_QUEUE = false;
	ENABLE_WEAR_LEVELING = false;
	BLOCK_MANAGER_ID = 0;
	GARBAGE_COLLECTION_POLICY = 0;
	MAX_CONCURRENT_GC_OPS = PACKAGE_SIZE * SSD_SIZE;
	GREED_SCALE = 2;
	ALLOW_DEFERRING_TRANSFERS = true;
	OVER_PROVISIONING_FACTOR = 0.7;

	OS_SCHEDULER = 0;

	READ_TRANSFER_DEADLINE = PAGE_READ_DELAY;// PAGE_READ_DELAY + 1;
}

void load_config(const char * const config_name) {
	FILE *config_file = NULL;

	/* update sscanf line below with max name length (%s) if changing sizes */
	uint line_size = 128;
	char line[line_size];
	uint line_number;

	char name[line_size];
	double value;

	if ((config_file = fopen(config_name, "r")) == NULL) {
		fprintf(stderr, "Config file %s not found.  Exiting.\n", config_name);
		exit(FILE_ERR);
	}

	for (line_number = 1; fgets(line, line_size, config_file) != NULL; line_number++) {
		line[line_size - 1] = '\0';

		/* ignore comments and blank lines */
		if (line[0] == '#' || line[0] == '\n')
			continue;

		/* read lines with entries (name value) */
		//printf("%s\n", line);
		if (sscanf(line, "\t%127s %lf", name, &value) == 2) {
			name[line_size - 1] = '\0';
			printf("%s    %f    %d\n", name, value, line_number);
			load_entry(name, value, line_number);
		} else
			fprintf(stderr, "Config file parsing error on line %u:  %s\n", line_number, line);
	}
	fclose(config_file);
}

void print_config(FILE *stream) {
	if (stream == NULL)
		stream = stdout;

	fprintf(stream, "#Operation Timings:\n");
	fprintf(stream, "\tBUS_CTRL_DELAY:\t%.16lf\n", BUS_CTRL_DELAY);
	fprintf(stream, "\tBUS_DATA_DELAY:\t%.16lf\n", BUS_DATA_DELAY);
	fprintf(stream, "\tPAGE_READ_DELAY:\t%.16lf\n", PAGE_READ_DELAY);
	fprintf(stream, "\tPAGE_WRITE_DELAY:\t%.16lf\n", PAGE_WRITE_DELAY);
	fprintf(stream, "\tBLOCK_ERASE_DELAY:\t%.16lf\n\n", BLOCK_ERASE_DELAY);

	fprintf(stream, "#SSD Architecture:\n");
	fprintf(stream, "\tSSD_SIZE:\t%u\n", SSD_SIZE);
	fprintf(stream, "\tPACKAGE_SIZE:\t%u\n", PACKAGE_SIZE);
	fprintf(stream, "\tDIE_SIZE:\t%u\n", DIE_SIZE);
	fprintf(stream, "\tPLANE_SIZE:\t%u\n", PLANE_SIZE);
	fprintf(stream, "\tBLOCK_SIZE:\t%u\n", BLOCK_SIZE);
	fprintf(stream, "\tPAGE_SIZE:\t%u\n\n", PAGE_SIZE);

	fprintf(stream, "\tMAX_SSD_QUEUE_SIZE:\t%u\n", MAX_SSD_QUEUE_SIZE);
	fprintf(stream, "\tOVER_PROVISIONING_FACTOR:\t%f\n", OVER_PROVISIONING_FACTOR);

	fprintf(stream, "#Controller:\n");
	fprintf(stream, "\tBLOCK_MANAGER_ID:\t%u\n", BLOCK_MANAGER_ID);
	fprintf(stream, "\tGREED_SCALE:\t%u\n", GREED_SCALE);
	fprintf(stream, "\tMAX_CONCURRENT_GC_OPS:\t%u\n", MAX_CONCURRENT_GC_OPS);
	fprintf(stream, "\tMAX_REPEATED_COPY_BACKS_ALLOWED: %i\n", MAX_REPEATED_COPY_BACKS_ALLOWED);
	fprintf(stream, "\tMAX_ITEMS_IN_COPY_BACK_MAP: %i\n\n", MAX_ITEMS_IN_COPY_BACK_MAP);
	fprintf(stream, "\tWRITE_DEADLINE: %i\n\n", WRITE_DEADLINE);
	fprintf(stream, "\tREAD_DEADLINE: %i\n\n", READ_DEADLINE);
	fprintf(stream, "\tENABLE_WEAR_LEVELING: %i\n\n", ENABLE_WEAR_LEVELING);

	fprintf(stream, "#Open Interface:\n");
	fprintf(stream, "\tENABLE_TAGGING: %i\n\n", ENABLE_TAGGING);

	fprintf(stream, "#Operating System:\n");
	fprintf(stream, "\tOS_SCHEDULER: %i\n\n", OS_SCHEDULER);

	fprintf(stream, "#Scheduler:\n");
	fprintf(stream, "\tALLOW_DEFERRING_TRANSFERS: %i\n", ALLOW_DEFERRING_TRANSFERS);
	fprintf(stream, "\tSCHEDULING_SCHEME: %i\n\n", SCHEDULING_SCHEME);

}

}
