#include "../ssd.h"
using namespace ssd;



// All we need to do to declare a workload is extend the Workload_Definition class and override the generate method
// This method returns a vector of threads, which the operating system will start running.
class Example_Workload : public Workload_Definition {
public:
	Example_Workload() : initialize_with_sequential_write(false) {}
	vector<Thread*> generate();
	bool initialize_with_sequential_write;
};

vector<Thread*> Example_Workload::generate() {
	vector<Thread*> starting_threads;
	vector<group_def> k_modes;
	k_modes.push_back(group_def(10, 50, 0));
	k_modes.push_back(group_def(90, 50, 1));

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


	t2->fixed_groups = true;
	t2->fac_num_ios_to_change_workload = 100;
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

int main()
{
	printf("Running EagleTree\n");
	set_small_SSD_config();
	SSD_SIZE = 4;
	PACKAGE_SIZE = 2;
	GREED_SCALE = 2;
	BLOCK_MANAGER_ID = 6;
	BLOCK_SIZE = 128;
	PRINT_LEVEL = 0;
	OVER_PROVISIONING_FACTOR = 0.7;
	GARBAGE_COLLECTION_POLICY = 0;
	Block_Manager_Groups::detector_type = 1;

	string name  = "/changing_workload/";
	Experiment::create_base_folder(name.c_str());
	string calib_name = "calib.txt";
	Example_Workload* workload = new Example_Workload();
	Experiment::calibrate_and_save(workload, calib_name, NUMBER_OF_ADDRESSABLE_PAGES(), true);


	Experiment::create_base_folder(name.c_str());
	Experiment* e = new Experiment();
	e->set_calibration_file(calib_name);

	//vector<pair<int, int> > groups;
	//groups.push_back(pair<int, int>(80, 0.2 * NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR));
	//groups.push_back(pair<int, int>(20, 0.8 * NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR));

	workload->initialize_with_sequential_write = true;

	e->set_workload(workload);
	e->set_io_limit(NUMBER_OF_ADDRESSABLE_PAGES() * 8);
	e->run("test");
	e->draw_graphs();
	delete workload;
	return 0;
}
