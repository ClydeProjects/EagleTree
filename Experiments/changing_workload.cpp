#include "../ssd.h"
using namespace ssd;

// All we need to do to declare a workload is extend the Workload_Definition class and override the generate method
// This method returns a vector of threads, which the operating system will start running.
class Example_Workload : public Workload_Definition {
public:
	Example_Workload() : initialize_with_sequential_write(false), fac_num_ios_to_change_workload(1) {}
	vector<Thread*> generate();
	bool initialize_with_sequential_write;
	double fac_num_ios_to_change_workload;
};

vector<Thread*> Example_Workload::generate() {
	vector<Thread*> starting_threads;
	vector<group_def> k_modes;
	/*k_modes.push_back(group_def(3.2, 20, 0));
	k_modes.push_back(group_def(6.4, 20, 1));
	k_modes.push_back(group_def(12.8, 20, 2));
	k_modes.push_back(group_def(25.6, 20, 3));
	k_modes.push_back(group_def(51.2, 20, 4));*/

	k_modes.push_back(group_def(100, 100, 0));
	//k_modes.push_back(group_def(90, 50, 1));

	/*k_modes.push_back(group_def(10, 33, 0));
	k_modes.push_back(group_def(40, 33, 1));
	k_modes.push_back(group_def(160, 33, 2));*/

	/*int num_groups = 10;
	for (int i = 0; i < num_groups; i++) {
		k_modes.push_back(group_def(100.0 / num_groups, 100.0 / num_groups, i));
	}*/

	bool include_init_message = Block_Manager_Groups::detector_type == 0;
	Thread* t1 = NULL;
	if (include_init_message) {
		t1 = new Initial_Message(k_modes);
	}
	else {
		t1 = new Initial_Message();
	}
	starting_threads.push_back(t1);

	// This workload begins with a large sequential write of the entire logical address space.
	K_Modal_Thread_Messaging* t2 = new K_Modal_Thread_Messaging(k_modes);

	t2->fixed_groups = false;
	t2->fac_num_ios_to_change_workload = 20;  // normally 6
	//t->set_io_size(1);

	if (initialize_with_sequential_write) {
		Simple_Thread* seq = new Synchronous_Sequential_Writer(0, NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR - 1);
		t1->add_follow_up_thread(seq);
		seq->add_follow_up_thread(t2);
	}
	else {
		t1->add_follow_up_thread(t2);
	}

	return starting_threads;
}


// All we need to do to declare a workload is extend the Workload_Definition class and override the generate method
// This method returns a vector of threads, which the operating system will start running.
class File_Workload : public Workload_Definition {
public:
	File_Workload() {}
	vector<Thread*> generate() {
		string file_name = "/home/niv/Desktop/EagleTree/changing_workload/file1";
		Thread* t1 = new File_Reading_Thread();
		vector<Thread*> starting_threads;
		starting_threads.push_back(t1);
		return starting_threads;
	}
};


int main() {
	printf("Running EagleTree\n");
	set_small_SSD_config();
	SSD_SIZE = 4;
	PACKAGE_SIZE = 2;
	GREED_SCALE = 2;
	BLOCK_MANAGER_ID = 6;
	BLOCK_SIZE = 128;
	PRINT_LEVEL = 0;
	//PLANE_SIZE = 1024;
	PLANE_SIZE = 1028;
	OVER_PROVISIONING_FACTOR = 0.7;
	GARBAGE_COLLECTION_POLICY = 1;
	Block_Manager_Groups::detector_type = 0;
	//Block_Manager_Groups::reclamation_threshold = SSD_SIZE * PACKAGE_SIZE * 2;
	Block_Manager_Groups::prioritize_groups_that_need_blocks = true;
	Block_Manager_Groups::garbage_collection_policy_within_groups = 1;
	bloom_detector::num_filters = 2;
	bloom_detector::min_num_groups = 6;
	bloom_detector::max_num_groups = 6;
	bloom_detector::bloom_false_positive_probability = 0.30;
	group::overprov_allocation_strategy = 0;
	PRINT_LEVEL = 0;
	MAX_SSD_QUEUE_SIZE = 1;

	string name  = "/changing_workload/";
	Experiment::create_base_folder(name.c_str());
	string calib_name = "calib.txt";

	Workload_Definition* workload = NULL;
	bool synthetic = true;
	if (synthetic) {
		Example_Workload* eg_workload = new Example_Workload();
		eg_workload->initialize_with_sequential_write = true;
		workload = eg_workload;
	}
	else {
		workload = new File_Workload();
	}

	//Experiment::calibrate_and_save(workload, calib_name, NUMBER_OF_ADDRESSABLE_PAGES() * 2, false);
	Experiment::create_base_folder(name.c_str());
	Experiment* e = new Experiment();
	//e->set_calibration_file(calib_name);

	//vector<pair<int, int> > groups;
	//groups.push_back(pair<int, int>(80, 0.2 * NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR));
	//groups.push_back(pair<int, int>(20, 0.8 * NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR));

	double min = 0.5;
	double max = 2;
	double incr = 2;
	//e->set_variable(&workload->fac_num_ios_to_change_workload, min, max, incr, "Periodicity");
	//e->set_exponential_increase(true);
	//e->set_variable(&Block_Manager_Groups::starvation_threshold, min, max, incr, "Writes Deadline (µs)");
	e->set_workload(workload);
	e->set_io_limit(NUMBER_OF_ADDRESSABLE_PAGES() * 6);
	e->run("test");
	e->draw_graphs();
	delete workload;
	return 0;
}
