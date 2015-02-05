#include "../ssd.h"
using namespace ssd;

// All we need to do to declare a workload is extend the Workload_Definition class and override the generate method
// This method returns a vector of threads, which the operating system will start running.
class Example_Workload : public Workload_Definition {
public:
	vector<Thread*> generate();
};

vector<Thread*> Example_Workload::generate() {

	// This workload begins with a large sequential write of the entire logical address space.
	Simple_Thread* init_write = new Synchronous_No_Collision_Random_Writer(min_lba, max_lba, 135);
	//Simple_Thread* init_write = new Synchronous_Sequential_Writer(min_lba, max_lba);
	init_write->set_io_size(1);
	init_write->set_num_ios(NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR);
	vector<Thread*> starting_threads;
	starting_threads.push_back(init_write);

	//init_write->set_num_ios(10000);

	// Once the sequential write above is finished, two threads start.
	// One performs random writes across the logical address space. The other performs random reads.
	// These workloads are synchronous. This means they operate with IO depth 1. The next IO is submitted to the IO
	// queue only once the previously subitted IO has finished.
	//
	// Each thread has an array of threads that it can invoke once it has finished executing all of its IOs.
	// This allows creating sophisticated workloads in a modular fashion.
	// The method add_follow_up_thread simply makes the init_write thread invoke the two other threads once it has finished.
	// The set_num_ios method is just the number of IOs each thread submits before they stop.
	// In this case, we let the threads continue submitting IOs indefinitely.
	// Nevertheless, in the main method below, we set the number of IOs that are allowed to occur before EagleTree shuts down
	// and prints statistics.
	int seed1 = 13515;
	int seed2 = 264;
	Simple_Thread* writer = new Synchronous_Random_Writer(min_lba, max_lba, seed1);
	Simple_Thread* reader = new Synchronous_Random_Reader(min_lba, max_lba, seed2);
	init_write->add_follow_up_thread(reader);
	init_write->add_follow_up_thread(writer);
	writer->set_num_ios(INFINITE);
	reader->set_num_ios(INFINITE);

	return starting_threads;
}

int main()
{
	printf("Running EagleTree\n");
	set_small_SSD_config();
	FTL_DESIGN = 1;	// using DFTL
	SCHEDULING_SCHEME = 7;
	PRINT_LEVEL = 0;
	MAX_SSD_QUEUE_SIZE = 16;
	ftl_cache::CACHED_ENTRIES_THRESHOLD = pow(2,15);		// SRAM fitting 256 kb, or 2^25 entries,
	DFTL::ENTRIES_PER_TRANSLATION_PAGE = 128;
	string name  = "/demo_output/";
	Experiment::create_base_folder(name.c_str());
	Experiment* e = new Experiment();
	Workload_Definition* workload = new Example_Workload();
	e->set_workload(workload);
	printf("NUMBER_OF_ADDRESSABLE_PAGES: %d  %d\n", NUMBER_OF_ADDRESSABLE_PAGES(), (int)(NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR));
	e->set_io_limit(3000000);
	e->run("test");
	e->draw_graphs();
	delete workload;
	return 0;
}
