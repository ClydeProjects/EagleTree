#include "../ssd.h"
using namespace ssd;

// All we need to do to declare a workload is extend the Workload_Definition class and override the generate method
// This method returns a vector of threads, which the operating system will start running.
class Example_Workload : public Workload_Definition {
public:
	vector<Thread*> generate();
};

vector<Thread*> Example_Workload::generate() {
	vector<Thread*> starting_threads;
	int seed2 = 264;
	Simple_Thread* writer = new Asynchronous_Random_Reader_Writer(min_lba, max_lba, seed2, 0.5);

	starting_threads.push_back(writer);
	//init_write->add_follow_up_thread(reader);
	writer->set_num_ios(INFINITE);
	return starting_threads;
}

void experiment(string exp_name) {
	string calib_name = "calib.txt";
	string base_folder  = "/" + exp_name + "/";
	Init_Workload* init = new Init_Workload();
	Experiment::create_base_folder(base_folder.c_str());
	Experiment::calibrate_and_save(init, calib_name, NUMBER_OF_ADDRESSABLE_PAGES() * 4);
	SCHEDULING_SCHEME = 0;
	Experiment* e = new Experiment();
	e->set_calibration_file(calib_name);
	Workload_Definition* workload = new Example_Workload();
	e->set_workload(workload);
	e->set_io_limit(NUMBER_OF_ADDRESSABLE_PAGES() * 10);
	e->run(exp_name);
	e->draw_graphs();
	delete workload;
	delete init;
}

int main()
{
	printf("Running EagleTree\n");
	set_small_SSD_config();
	SSD_SIZE = 4;
	PACKAGE_SIZE = 2;
	SCHEDULING_SCHEME = 1;
	GREED_SCALE = 2;
	BLOCK_MANAGER_ID = 0;
	BLOCK_SIZE = 128;
	GARBAGE_COLLECTION_POLICY = 0;
	PRINT_LEVEL = 0;
	OVER_PROVISIONING_FACTOR = 0.7;
	experiment("Greedy");
	//system("cd ../..");
	GARBAGE_COLLECTION_POLICY = 1;
	experiment("LRU");
	return 0;
}
