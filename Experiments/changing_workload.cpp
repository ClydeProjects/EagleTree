#include "../ssd.h"
using namespace ssd;

class Initial_Message : public Thread
{
public:
	Initial_Message(vector<group_def> k_modes) : k_modes(k_modes) {}
	Initial_Message() : k_modes() {}
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
    vector<group_def> k_modes;
};

class K_Modal_Thread_Messaging : public K_Modal_Thread
{
public:
	K_Modal_Thread_Messaging(vector<group_def> k_modes) : K_Modal_Thread(k_modes), counter(0), fac_num_ios_to_change_workload(2) {}
	K_Modal_Thread_Messaging() : K_Modal_Thread(), counter(0), fac_num_ios_to_change_workload(2) {}
	void issue_first_IOs();
	void handle_event_completion(Event* event);
    friend class boost::serialization::access;
    template<class Archive> void serialize(Archive & ar, const unsigned int version) {
    	ar & boost::serialization::base_object<K_Modal_Thread>(*this);
    }
    double fac_num_ios_to_change_workload;
private:
	long counter;
};

void K_Modal_Thread_Messaging::issue_first_IOs() {
	Groups_Message* gm = new Groups_Message(get_time());
	gm->groups = k_modes;
	K_Modal_Thread::issue_first_IOs();
}

void K_Modal_Thread_Messaging::handle_event_completion(Event* event) {
	if (++counter % (int)(NUMBER_OF_ADDRESSABLE_PAGES() * fac_num_ios_to_change_workload) == 0) {
		//printf("%d    %d    %d   \n", counter, NUMBER_OF_ADDRESSABLE_PAGES() * 8, counter % NUMBER_OF_ADDRESSABLE_PAGES() * 8 == 0);
		int prob_temp = k_modes[0].update_frequency;
		k_modes[0].update_frequency = k_modes[1].update_frequency;
		k_modes[1].update_frequency = prob_temp;
		int tag_temp = k_modes[0].tag;
		k_modes[0].tag = k_modes[1].tag;
		k_modes[1].tag = tag_temp;
		/*Groups_Message* gm = new Groups_Message(get_time());
		gm->redistribution_of_update_frequencies = true;
		gm->groups = k_modes;
		submit(gm);*/
	}
	K_Modal_Thread::issue_first_IOs();
}

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
	k_modes.push_back(group_def(30, 80, 0));
	k_modes.push_back(group_def(70, 20, 1));

	Initial_Message* t1 = new Initial_Message(k_modes);
	starting_threads.push_back(t1);

	//k_modes[0].size = 80;
	//k_modes[1].size = 20;

	// This workload begins with a large sequential write of the entire logical address space.
	K_Modal_Thread_Messaging* t2 = new K_Modal_Thread_Messaging(k_modes);
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
	string name  = "/demo_output/";
	Experiment::create_base_folder(name.c_str());
	Experiment* e = new Experiment();

	//vector<pair<int, int> > groups;
	//groups.push_back(pair<int, int>(80, 0.2 * NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR));
	//groups.push_back(pair<int, int>(20, 0.8 * NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR));
	Example_Workload* workload = new Example_Workload();
	workload->initialize_with_sequential_write = true;

	e->set_workload(workload);
	e->set_io_limit(NUMBER_OF_ADDRESSABLE_PAGES() * 15);
	e->run("test");
	e->draw_graphs();
	delete workload;
	return 0;
}
