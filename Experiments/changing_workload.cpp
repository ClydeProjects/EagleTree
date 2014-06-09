#include "../ssd.h"
using namespace ssd;

class Initial_Message : public K_Modal_Thread
{
public:
	Initial_Message(vector<pair<int, int> > k_modes) : K_Modal_Thread(k_modes) {}
	Initial_Message() : K_Modal_Thread() {}
	void issue_first_IOs() {
		Groups_Message* gm = new Groups_Message(get_time());
		gm->groups = k_modes;
		submit(gm);
	}
	void handle_event_completion(Event* event) {}
    friend class boost::serialization::access;
    template<class Archive> void serialize(Archive & ar, const unsigned int version) {
    	ar & boost::serialization::base_object<K_Modal_Thread>(*this);
    }
};

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
	vector<pair<int, int> > k_modes;
	k_modes.push_back(pair<int, int>(20, 80));
	k_modes.push_back(pair<int, int>(80, 20));

	Initial_Message* t1 = new Initial_Message(k_modes);
	starting_threads.push_back(t1);

	// This workload begins with a large sequential write of the entire logical address space.
	Thread* t2 = new K_Modal_Thread_Messaging(k_modes);
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
	string name  = "/demo_output/";
	Experiment::create_base_folder(name.c_str());
	Experiment* e = new Experiment();

	//vector<pair<int, int> > groups;
	//groups.push_back(pair<int, int>(80, 0.2 * NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR));
	//groups.push_back(pair<int, int>(20, 0.8 * NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR));
	Example_Workload* workload = new Example_Workload();
	workload->initialize_with_sequential_write = true;

	e->set_workload(workload);
	e->set_io_limit(NUMBER_OF_ADDRESSABLE_PAGES() * 6);
	e->run("test");
	e->draw_graphs();
	delete workload;
	return 0;
}
