#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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

/* Ram class:
 * 	delay to read from and write to the RAM for 1 page of data */
double RAM_READ_DELAY = 0.00000001;
double RAM_WRITE_DELAY = 0.00000001;

/* Bus class:
 * 	delay to communicate over bus
 * 	max number of connected devices allowed
 * 	number of time entries bus has to keep track of future schedule usage
 * 	value used as a flag to indicate channel is free
 * 		(use a value not used as a delay value - e.g. -1.0)
 * 	number of simultaneous communication channels - defined by SSD_SIZE */
double BUS_CTRL_DELAY = 0.000000005;
double BUS_DATA_DELAY = 0.00000001;
/* uint BUS_CHANNELS = 4; same as # of Packages, defined by SSD_SIZE */

/* Ssd class:
 * 	number of Packages per Ssd (size) */
uint SSD_SIZE = 4;

/* Package class:
 * 	number of Dies per Package (size) */
uint PACKAGE_SIZE = 8;

/* Die class:
 * 	number of Planes per Die (size) */
uint DIE_SIZE = 2;

/* Plane class:
 * 	number of Blocks per Plane (size)
 * 	delay for reading from plane register
 * 	delay for writing to plane register
 * 	delay for merging is based on read, write, reg_read, reg_write 
 * 		and does not need to be explicitly defined */
uint PLANE_SIZE = 64;
double PLANE_REG_READ_DELAY = 0.0000000001;
double PLANE_REG_WRITE_DELAY = 0.0000000001;

/* Block class:
 * 	number of Pages per Block (size)
 * 	number of erases in lifetime of block
 * 	delay for erasing block */
uint BLOCK_SIZE = 16;
uint BLOCK_ERASES = 1048675;
double BLOCK_ERASE_DELAY = 0.001;

/* Page class:
 * 	delay for Page reads
 * 	delay for Page writes */
double PAGE_READ_DELAY = 0.000001;
double PAGE_WRITE_DELAY = 0.00001;

/* Page data memory allocation
 *
 */
uint PAGE_SIZE = 4096;
bool PAGE_ENABLE_DATA = false;

int OS_SCHEDULER = 0;

/*
 * Memory area to support pages with data.
 */
void *page_data;

/*
 * Number of blocks to reserve for mappings. e.g. map directory in BAST.
 */
uint MAP_DIRECTORY_SIZE = 0;

/*
 * Implementation to use (0 -> Page, 1 -> BAST, 2 -> FAST, 3 -> DFTL, 4 -> BiModal
 */
uint FTL_IMPLEMENTATION = 0;

/*
 * Limit of LOG pages (for use in BAST)
 */
uint BAST_LOG_PAGE_LIMIT = 100;


/*
 * Limit of LOG pages (for use in FAST)
 */
uint FAST_LOG_PAGE_LIMIT = 4;

/*
 * Number of pages allowed to be in DFTL Cached Mapping Table.
 * (Size equals CACHE_BLOCK_LIMIT * block size * page size)
 *
 */
uint CACHE_DFTL_LIMIT = 8;

/*
 * Parallelism mode.
 * 0 -> Normal
 * 1 -> Striping
 * 2 -> Logical Address Space Parallelism (LASP)
 */
uint PARALLELISM_MODE = 0;

/* Virtual block size (as a multiple of the physical block size) */
//uint VIRTUAL_BLOCK_SIZE = 1;

/* Virtual page size (as a multiple of the physical page size) */
//uint VIRTUAL_PAGE_SIZE = 1;

uint NUMBER_OF_ADDRESSABLE_BLOCKS = 0;

/* RAISSDs: Number of physical SSDs */
uint RAID_NUMBER_OF_PHYSICAL_SSDS = 0;

bool USE_ERASE_QUEUE = false;


/*
 * Scheduling scheme
 * 0 ->  Naive: Read Command, Read Transfer, Write, GC, Erase
 * 1 ->  Experimental
 * 2 ->  Smart
 */
int SCHEDULING_SCHEME = 2;

bool ENABLE_WEAR_LEVELING = false;
int WEAR_LEVEL_THRESHOLD = 100;
int MAX_ONGOING_WL_OPS = 1;
int MAX_CONCURRENT_GC_OPS = 1;

/*
 * Block manager
 * 0 -> Shortest Queues
 * 1 -> Shortest Queues with Hot/Cold data seperation
 * 2 -> Wearwolf
 * 3 -> SQ with Locality
 * 4 -> Round Robin
 */
int BLOCK_MANAGER_ID = 3;

bool GREED_SCALE = 2;

/* Output level of detail:
 * 0 -> Nothing
 * 1 -> Semi-detailed
 * 2 -> Detailed
 */
int PRINT_LEVEL = 0;
int PRINT_FILE_MANAGER_INFO = false;

bool OS_LOCK = false;
int WEARWOLF_LOCALITY_THRESHOLD = 10;
bool ENABLE_TAGGING = false;

bool ALLOW_DEFERRING_TRANSFERS = true;

double OVER_PROVISIONING_FACTOR = 0.7;

/* Defines the max number of copy back operations on a page before ECC check is performed.
 * Set to zero to disable copy back GC operations */
uint MAX_REPEATED_COPY_BACKS_ALLOWED = 0;

/* Defines the max number of page addresses in map keeping track of each pages copy back count */
uint MAX_ITEMS_IN_COPY_BACK_MAP = 1024;

/* Defines the maximal length of the SSD queue  */
int MAX_SSD_QUEUE_SIZE = 15;

int WRITE_DEADLINE = 10000000;
int READ_DEADLINE =  10000000;
int READ_TRANSFER_DEADLINE = 10000000;

/* Defines the maximal number of locks that can be held by the OS  */
uint MAX_OS_NUM_LOCKS = 1000;

/* Defines how the sequential writes detection algorithm spreads a sequential write  */
uint LOCALITY_PARALLEL_DEGREE = 0;

int PAGE_HOTNESS_MEASURER = 0;

void load_entry(char *name, double value, uint line_number) {
	/* cheap implementation - go through all possibilities and match entry */
	if (!strcmp(name, "RAM_READ_DELAY"))
		RAM_READ_DELAY = value;
	else if (!strcmp(name, "RAM_WRITE_DELAY"))
		RAM_WRITE_DELAY = value;
	else if (!strcmp(name, "BUS_CTRL_DELAY"))
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
	else if (!strcmp(name, "PLANE_REG_READ_DELAY"))
		PLANE_REG_READ_DELAY = value;
	else if (!strcmp(name, "PLANE_REG_WRITE_DELAY"))
		PLANE_REG_WRITE_DELAY = value;
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
	else if (!strcmp(name, "FTL_IMPLEMENTATION"))
		FTL_IMPLEMENTATION = value;
	else if (!strcmp(name, "PAGE_ENABLE_DATA"))
		PAGE_ENABLE_DATA = (value == 1);
	else if (!strcmp(name, "MAP_DIRECTORY_SIZE"))
		MAP_DIRECTORY_SIZE = value;
	else if (!strcmp(name, "FTL_IMPLEMENTATION"))
		FTL_IMPLEMENTATION = value;
	else if (!strcmp(name, "BAST_LOG_PAGE_LIMIT"))
		BAST_LOG_PAGE_LIMIT = value;
	else if (!strcmp(name, "FAST_LOG_PAGE_LIMIT"))
		FAST_LOG_PAGE_LIMIT = value;
	else if (!strcmp(name, "CACHE_DFTL_LIMIT"))
		CACHE_DFTL_LIMIT = value;
	else if (!strcmp(name, "PARALLELISM_MODE"))
		PARALLELISM_MODE = value;
	else if (!strcmp(name, "RAID_NUMBER_OF_PHYSICAL_SSDS"))
		RAID_NUMBER_OF_PHYSICAL_SSDS = value;
	else if (!strcmp(name, "MAX_REPEATED_COPY_BACKS_ALLOWED"))
		MAX_REPEATED_COPY_BACKS_ALLOWED = value;
	else if (!strcmp(name, "MAX_ITEMS_IN_COPY_BACK_MAP"))
		MAX_ITEMS_IN_COPY_BACK_MAP = value;
	else
		fprintf(stderr, "Config file parsing error on line %u\n", line_number);
	return;
}

void set_normal_config() {
	SSD_SIZE = 4;
	PACKAGE_SIZE = 4;
	DIE_SIZE = 1;
	PLANE_SIZE = 64;
	BLOCK_SIZE = 32;

	/*PAGE_READ_DELAY = 115 ;
	PAGE_WRITE_DELAY = 1600 ;
	BUS_CTRL_DELAY = 5 ;
	BUS_DATA_DELAY = 350 ;
	BLOCK_ERASE_DELAY = 3000 ;*/

	PAGE_READ_DELAY = 115 / 5;
	PAGE_WRITE_DELAY = 1600 / 5;
	BUS_CTRL_DELAY = 5 / 5;
	BUS_DATA_DELAY = 350 / 5;
	BLOCK_ERASE_DELAY = 3000 / 5;

	MAX_SSD_QUEUE_SIZE = 32;
	MAX_REPEATED_COPY_BACKS_ALLOWED = 0;
	SCHEDULING_SCHEME = 0;  // FIFO

	USE_ERASE_QUEUE = false;
	ENABLE_WEAR_LEVELING = false;
	BLOCK_MANAGER_ID = 0;
	MAX_CONCURRENT_GC_OPS = PACKAGE_SIZE * SSD_SIZE;
	GREED_SCALE = 2;
	ALLOW_DEFERRING_TRANSFERS = true;
	OVER_PROVISIONING_FACTOR = 0.6;

	OS_SCHEDULER = 0;

	READ_TRANSFER_DEADLINE = PAGE_READ_DELAY + 1;// PAGE_READ_DELAY + 1;
}

void set_big_SSD() {
	SSD_SIZE = 8;
	PACKAGE_SIZE = 8;
	DIE_SIZE = 1;
	PLANE_SIZE = 1028;
	BLOCK_SIZE = 256;

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
	MAX_CONCURRENT_GC_OPS = PACKAGE_SIZE * SSD_SIZE;
	GREED_SCALE = 2;
	ALLOW_DEFERRING_TRANSFERS = true;
	OVER_PROVISIONING_FACTOR = 0.6;

	OS_SCHEDULER = 0;

	READ_TRANSFER_DEADLINE = PAGE_READ_DELAY + 1;// PAGE_READ_DELAY + 1;
}

void load_config() {
	const char * const config_name = "ssd.conf";
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
		if (sscanf(line, "%127s %lf", name, &value) == 2) {
			name[line_size - 1] = '\0';
			load_entry(name, value, line_number);
		} else
			fprintf(stderr, "Config file parsing error on line %u\n",
					line_number);
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
	fprintf(stream, "\tFTL_IMPLEMENTATION: %i\n", FTL_IMPLEMENTATION);
	fprintf(stream, "\tMAX_REPEATED_COPY_BACKS_ALLOWED: %i\n", MAX_REPEATED_COPY_BACKS_ALLOWED);
	fprintf(stream, "\tMAX_ITEMS_IN_COPY_BACK_MAP: %i\n\n", MAX_ITEMS_IN_COPY_BACK_MAP);

	fprintf(stream, "#Operating System:\n");
	fprintf(stream, "\tOS_SCHEDULER: %i\n\n", OS_SCHEDULER);

	fprintf(stream, "#Scheduler:\n");
	fprintf(stream, "\tALLOW_DEFERRING_TRANSFERS: %i\n", ALLOW_DEFERRING_TRANSFERS);
	fprintf(stream, "\tSCHEDULING_SCHEME: %i\n\n", SCHEDULING_SCHEME);

}

}
