/*
 * ssd_synchronous_writer_thread.cpp
 *
 *  Created on: Jul 20, 2012
 *      Author: niv
 */

#include "../ssd.h"
//#include "../MTRand/mtrand.h"
#include <stdlib.h>
using namespace ssd;

// =================  Thread =============================

bool Thread::record_internal_statistics = false;

Thread::Thread() :
		finished(false), time(1), threads_to_start_when_this_thread_finishes(),
		os(NULL), internal_statistics_gatherer(new StatisticsGatherer()),
		external_statistics_gatherer(NULL), num_IOs_executing(0), io_queue(), stopped(false) {}

Thread::~Thread() {
	for (auto t : threads_to_start_when_this_thread_finishes) {
		delete t;
	}
	delete internal_statistics_gatherer;
}

Event* Thread::pop() {
	if (io_queue.size() == 0) {
		return NULL;
	} else {
		Event* next = io_queue.front();
		io_queue.pop();
		return next;
	}
}

Event* Thread::peek() const {
	return io_queue.size() == 0 ? NULL : io_queue.front();
}

void Thread::init(OperatingSystem* new_os, double new_time) {
	os = new_os;
	time = new_time;
	//deque<Event*> empty;
	//swap(empty, submitted_events);
	issue_first_IOs();
	/*for (uint i = 0; i < submitted_events.size() && is_experiment_thread(); i++) {
		Event* e = submitted_events[i];
		if (e != NULL) e->set_experiment_io(true);
	}*/
	//
	//num_IOs_executing += submitted_events.size();
	//queue.insert(queue.begin(), submitted_events.begin(), submitted_events.end());
}

void Thread::register_event_completion(Event* event) {
	//deque<Event*> empty;
	//swap(empty, submitted_events);
	num_IOs_executing--;
	if (record_internal_statistics) {
		internal_statistics_gatherer->register_completed_event(*event);
	}
	if (external_statistics_gatherer != NULL) {
		external_statistics_gatherer->register_completed_event(*event);
	}
	time = event->get_current_time();

	handle_event_completion(event);
	if (num_IOs_executing == 0) {
		handle_no_IOs_left();
		if (num_IOs_executing == 0 && !stopped) {
			finished = true;
		}
	}
}

bool Thread::is_finished() const {
	return finished;
}

bool Thread::can_submit_more() const {
	return io_queue.size() < NUMBER_OF_ADDRESSABLE_PAGES();
}

void Thread::stop() {
	stopped = true;
}

bool Thread::is_stopped() const {
	return stopped;
}

void Thread::submit(Event* event) {
	if (finished || stopped) {
		delete event;
		return;
	}
	event->set_start_time(event->get_current_time());
	io_queue.push(event);
	num_IOs_executing++;
	if (!can_submit_more()) {
		printf("Reached the maximum of events that can be submitted at the same time: %d\n", io_queue.size());
		assert(false);
	}
}

void Thread::set_statistics_gatherer(StatisticsGatherer* new_statistics_gatherer) {
	external_statistics_gatherer = new_statistics_gatherer;
}

// =================  Simple_Thread  =============================

Simple_Thread::Simple_Thread(IO_Pattern* generator, IO_Mode_Generator* mode_gen, int MAX_OUTSTANDING_IOS, long num_IOs)
	: Thread(),
	  MAX_IOS(MAX_OUTSTANDING_IOS),
	  io_gen(generator),
	  io_type_gen(mode_gen),
	  number_of_times_to_repeat(num_IOs),
	  io_size(1)
{
	assert(MAX_IOS > 0);
}

Simple_Thread::Simple_Thread(IO_Pattern* generator, int MAX_IOS, IO_Mode_Generator* mode_gen)
	: Thread(),
	  MAX_IOS(MAX_IOS),
	  io_gen(generator),
	  io_type_gen(mode_gen),
	  io_size(1)
{
	assert(MAX_IOS > 0);
	number_of_times_to_repeat = generator->max_LBA - generator->min_LBA + 1;
}

Simple_Thread::~Simple_Thread() {
	delete io_gen;
	delete io_type_gen;
}

void Simple_Thread::generate_io() {
	while (get_num_ongoing_IOs() < MAX_IOS && number_of_times_to_repeat > 0 && !is_finished() && !is_stopped()) {
		number_of_times_to_repeat--;
		event_type type = io_type_gen->next();
		long logical_addr = io_gen->next();
		Event* e = new Event(type, logical_addr, io_size, get_current_time());
		submit(e);
	}

}

void Simple_Thread::issue_first_IOs() {
	generate_io();
}

void Simple_Thread::handle_event_completion(Event* event) {
	if (number_of_times_to_repeat > 0) {
		generate_io();
	}
}

// =================  Flexible_Reader_Thread  =============================

Flexible_Reader_Thread::Flexible_Reader_Thread(long min_LBA, long max_LBA, int repetitions_num)
	: Thread(),
	  min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  ready_to_issue_next_write(true),
	  number_of_times_to_repeat(repetitions_num),
	  flex_reader(NULL)
{}

void Flexible_Reader_Thread::issue_first_IOs() {
	if (flex_reader == NULL) {
		vector<Address_Range> ranges;
		ranges.push_back(Address_Range(min_LBA, max_LBA));
		assert(os != NULL);
		flex_reader = os->create_flexible_reader(ranges);
	}
	if (ready_to_issue_next_write && number_of_times_to_repeat > 0) {
		ready_to_issue_next_write = false;
		Event* e = flex_reader->read_next(get_current_time());
		submit(e);
	}
}

void Flexible_Reader_Thread::handle_event_completion(Event* event) {
	assert(!ready_to_issue_next_write);
	ready_to_issue_next_write = true;
	if (flex_reader->is_finished()) {
		delete flex_reader;
		flex_reader = NULL;
		if (--number_of_times_to_repeat == 0) {
			//finished = true;
			//StateVisualiser::print_page_status();
		}
	}
}

// =================  Collision_Free_Asynchronous_Random_Writer  =============================

/*Collision_Free_Asynchronous_Random_Thread::Collision_Free_Asynchronous_Random_Thread(long min_LBA, long max_LBA, int num_ios_to_issue, ulong randseed, event_type type)
	: Thread(),
	  min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  number_of_times_to_repeat(num_ios_to_issue),
	  type(type),
	  random_number_generator(randseed)
{}

void Collision_Free_Asynchronous_Random_Thread::issue_first_IOs() {
	Event* event;
	if (0 < number_of_times_to_repeat) {
		long address;
		do {
			address = min_LBA + random_number_generator() % (max_LBA - min_LBA + 1);
		} while (logical_addresses_submitted.count(address) == 1);
		printf("num events submitted:  %d\n", logical_addresses_submitted.size());
		logical_addresses_submitted.insert(address);
		event =  new Event(type, address, 1, get_current_time());
	} else {
		event = NULL;
	}
	number_of_times_to_repeat--;
	//return event;
}

void Collision_Free_Asynchronous_Random_Thread::handle_event_completion(Event* event) {
	logical_addresses_submitted.erase(event->get_logical_address());
}*/

void K_Modal_Thread::create_io() {
	double total_prob = 0;
	for (int i = 0; i < k_modes.size(); i++) {
		total_prob += k_modes[i].update_frequency;
	}

	double group_decider = random_number_generator() * total_prob;
	double cumulative_prob = 0;
	int selected_group_start_lba = 0;
	int selected_group_size = 0;
	static vector<int> group_hist(k_modes.size());
	int group_index = UNDEFINED;
	for (int i = 0; i < k_modes.size(); i++) {
		cumulative_prob += k_modes[i].update_frequency;
		if (group_decider < cumulative_prob) {
			selected_group_size = (k_modes[i].size / 100.0) * NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR;
			group_hist[i]++;
			group_index = i;
			break;
		}
		selected_group_start_lba += (k_modes[i].size / 100.0) * NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR;
	}
	assert(group_index != UNDEFINED);
	//printf("%d  %d\n", group_hist[0], group_hist[1]);
	long lba = selected_group_start_lba + random_number_generator() * selected_group_size;

	Event* e = new Event(WRITE, lba, 1, get_time());
	if (k_modes[group_index].tag == UNDEFINED) {
		e->set_tag(selected_group_start_lba);
	}
	else {
		e->set_tag(k_modes[group_index].tag);
	}
	//e->set_size(selected_group_size);
	submit(e);
}

void K_Modal_Thread::issue_first_IOs() {
	while (get_num_ongoing_IOs() < 1 && !is_finished() && !is_stopped()) {
		create_io();
	}
}

void K_Modal_Thread::handle_event_completion(Event* event) {
	issue_first_IOs();
}

void K_Modal_Thread_Messaging::issue_first_IOs() {
	Groups_Message* gm = new Groups_Message(get_time());
	gm->groups = k_modes;
	K_Modal_Thread::issue_first_IOs();
}

void K_Modal_Thread_Messaging::handle_event_completion(Event* event) {
	if (++counter % (int)(NUMBER_OF_ADDRESSABLE_PAGES() * fac_num_ios_to_change_workload) == 0) {
		//printf("%d    %d    %d   \n", counter, NUMBER_OF_ADDRESSABLE_PAGES() * 8, counter % NUMBER_OF_ADDRESSABLE_PAGES() * 8 == 0);
		int swap1 = 0;
		int swap2 = 1;
		int prob_temp = k_modes[swap1].update_frequency;
		k_modes[swap1].update_frequency = k_modes[swap2].update_frequency;
		k_modes[swap2].update_frequency = prob_temp;
		fac_num_ios_to_change_workload = 100;
		if (fixed_groups) {
			int tag_temp = k_modes[swap1].tag;
			k_modes[swap1].tag = k_modes[swap2].tag;
			k_modes[swap2].tag = tag_temp;
		}
		else {
			Groups_Message* gm = new Groups_Message(get_time());
			gm->redistribution_of_update_frequencies = true;
			gm->groups = k_modes;
			submit(gm);
		}
	}
	K_Modal_Thread::issue_first_IOs();
}

File_Reading_Thread::File_Reading_Thread() :
		file_name("/home/niv/Desktop/EagleTree/changing_workload/file48"),
		file(file_name),
		io_num(0),
		address_map(),
		highest_address(0) {
	printf("file name: %s \n", file_name.c_str());
	print_trace_stats();
	//sequentiality_analyze();
	process_trace();
	//calc_update_distances();
}

int File_Reading_Thread::get_next() {
	string line = "";
	if (getline(file,line)) {
		int num_log_writes = std::count(line.begin(), line.end(), 'l');
		string number = line;
		if (num_log_writes > 0) {
			number = number.substr(num_log_writes,number.size());
		}
		int addr = atoi(number.c_str());
		if (addr < 0) {
			int i = 0;
			i++;
		}
		return addr;
	}
	return -10000;
}

void File_Reading_Thread::get_back_to_start_after_init() {
	file.close();
	file.open(file_name.c_str());
	string line = "";
	int line_num = 0;
	while (getline(file,line)) {
		if (line.find("Loading finished") != string::npos) {
			printf("loading finished at line %d\n", line_num);
			break;
		}
		line_num++;
	}
	line_num *= 4;
	while (getline(file,line) && line_num-- > 0);
}

void File_Reading_Thread::process_trace() {
	// find out at which IO number and line number in the file the initialization phase ends.
	file.open(file_name.c_str());
	string line = "";
	int last_line_of_init_phase = 0;
	int last_io_num = 0;
	while (getline(file,line)) {
		if (line.find("Loading finished") != string::npos) {
			printf("loading finished at line %d   and after %d ios\n", last_line_of_init_phase, last_io_num);
			break;
		}
		int addr = atoi(line.c_str());
		if (addr >= 0) {
			last_io_num++;
		}
		last_line_of_init_phase++;
	}


	file.close();

	file.open(file_name.c_str());

	int addr = UNDEFINED;

	int counter = 1;
	while ((addr = get_next()) != -10000 && unique_addresses.size() < NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR) {
		if (addr >= 0) {
			unique_addresses.insert(addr);
			if (addr > highest_address) {
				highest_address = addr;
			}
		}
	}
	file.close();
	update_frequency_count = vector<int>(highest_address, 0);
	file.open(file_name.c_str());
	int io_num = 0;
	while ((addr = get_next()) != -10000) {
		if (unique_addresses.count(addr) == 1  && io_num > last_io_num * 2) {
			update_frequency_count[addr]++;
		}
		if (addr >= 0) {
			io_num++;
		}
	}
	file.close();

	printf("OVER_PROVISIONING_FACTOR:  %f\n", OVER_PROVISIONING_FACTOR);
	printf("NUMBER_OF_ADDRESSABLE_PAGES():  %d\n", NUMBER_OF_ADDRESSABLE_PAGES());
	printf("NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR:  %f\n", NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR);
	printf("Num unique addresses: %d\n", unique_addresses.size());
	printf("Num ios in trace: %d\n", io_num);

	address_map = vector<int>(highest_address, UNDEFINED);

	counter = 0;
	for (auto i : unique_addresses) {
		if (i <= highest_address && counter < unique_addresses.size()) {
			address_map[i] = counter;
			//printf("%d -> %d\n", i, counter);
			counter++;
			assert(counter <= NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR + 1);
		}
	}
	printf("finished processing\n");

	//unique_addresses.clear();

}

struct seq {
	seq(int starting_address, int size, int time) : starting_address(starting_address), size(size), time(time) {}
	int starting_address;
	int size;
	int time;
};

void File_Reading_Thread::sequentiality_analyze() {
	file.open(file_name.c_str());
	int addr = 0;
	vector<int> sequential;
	int last_addr = 0;
	int current_sequential_size = 0;
	vector<int> num_sequential_address_was_in = vector<int>(2000000, -1);
	vector<vector<int> > sequential_address_ids_addr_was_in = vector<vector<int> >(2000000, vector<int>());
	vector<seq* > sequential_address_seq = vector<seq* >();
	int id = 0;
	int io_num = 0;
	while ((addr = get_next()) != -10000) {
		if (addr < 0) {
			continue;
		}
		//printf("%d\n", addr);
		if (addr == last_addr + 1) {
			current_sequential_size++;
			num_sequential_address_was_in[last_addr]++;
			sequential_address_ids_addr_was_in[last_addr].push_back(id);
		}
		else {
			num_sequential_address_was_in[last_addr]++;
			sequential_address_ids_addr_was_in[last_addr].push_back(id);
			if (current_sequential_size > 0) {
				sequential.push_back(current_sequential_size + 1);
				seq* p = new seq(last_addr - current_sequential_size, current_sequential_size + 1, io_num);
				sequential_address_seq.push_back(p);
				id++;
			}
			current_sequential_size = 0;
		}
		last_addr = addr;
		io_num++;
	}
	file.close();

	stable_sort(sequential_address_seq.begin(), sequential_address_seq.end(), [](seq* a, seq* b) { return a->starting_address < b->starting_address; });
	int num_overlaps = 0;
	for (int i = 1; i < sequential_address_seq.size(); i++) {

		seq* now = sequential_address_seq[i];
		seq* last = sequential_address_seq[i-1];
		printf("%d: %d  %d\n", now->starting_address, now->size, now->time);

		if (now->starting_address >= last->starting_address + last->size) {

		}
		else if (now->starting_address + now->size < last->starting_address + last->size) {
			num_overlaps += now->size;
		}
		else {
			num_overlaps += (last->starting_address + last->size) - now->starting_address;
		}
	}

	printf("num sequential overlaps: %d\n", num_overlaps);

	map<int, int> skew;
	double max = 0;
	int total_writes_included_in_seq = 0;
	for (auto i : sequential) {
		total_writes_included_in_seq += i;
		if (i > max) {
			max = i;
		}
		skew[i]++;
	}
	double avg = total_writes_included_in_seq / sequential.size();
	printf("max sequential write size: %f\n", max);
	printf("avg sequential write: %f\n", avg);
	printf("median sequential write: %d\n", sequential[sequential.size() / 2]);
	printf("num sequential writes: %d\n", sequential.size());
	printf("single page writes included in seq: %d\n", total_writes_included_in_seq);
	printf("skew sequential:\n");
	for (auto i : skew) {
		printf("%d\t%d\n", i.first, i.second);
	}
	/*printf("which addresses were in sequential writes\n");
	for (int i = 0; i < num_sequential_address_was_in.size(); i++) {
		if (i > 0 && num_sequential_address_was_in[i-1] == 0 && num_sequential_address_was_in[i] > 0) {
			printf("\n");
		}
		if (num_sequential_address_was_in[i] > 0) {
			printf("%d  %d\t", i, num_sequential_address_was_in[i]);
			for (auto j : sequential_address_ids_addr_was_in[i]) {
				printf("%d ", j);
			}
			printf("\n");
		}
	}
	printf("finished\n");*/
}

void File_Reading_Thread::calc_update_distances() {
	vector<vector<int> > update_distances(highest_address + 100, vector<int>());
	file.open(file_name.c_str());
	int addr = 0;
	int io_num = 0;
	while ((addr = get_next()) != -10000) {
		if (unique_addresses.count(addr) == 1) {
			assert(highest_address >= addr);
			update_distances[addr].push_back(io_num++);
		}
	}

	map<int, int> skew;
	map<double, double> averages;
	ofstream addresses_stream_init_addresses;
	ofstream addresses_stream_added_addresses;
	addresses_stream_init_addresses.open("/home/niv/Desktop/EagleTree/changing_workload/update_distances_init.csv");
	addresses_stream_added_addresses.open("/home/niv/Desktop/EagleTree/changing_workload/update_distances_added.csv");
	addresses_stream_init_addresses << "address, size" << ", " << "MaxDistance" << ", " << "Avg" << ", firstWrite, lastWrite, sizeTimesAvg" << endl;
	addresses_stream_added_addresses << "address, size" << ", " << "MaxDistance" << ", " << "Avg" << ", firstWrite, lastWrite, sizeTimesAvg" << endl;
	for (int i = 0; i < update_distances.size(); i++) {
		if ( update_distances[i].size() > 0) {
			double avg = 0;
			for (int j = 1; j < update_distances[i].size(); j++) {
				avg += update_distances[i][j] - update_distances[i][j-1];
			}
			if (update_distances[i].size() > 1) {
				avg /= update_distances[i].size() - 1;
			}
			int max_distance = update_distances[i].back() - update_distances[i].front();
			if (initial_addresses.count(i) == 1) {
				addresses_stream_init_addresses << i << ", " << update_distances[i].size() << ", " << max_distance << ", " << avg << ", " << update_distances[i].front() << ", " << update_distances[i].back() << ", " << avg * update_distances[i].size() <<  endl;
			}
			else {
				addresses_stream_added_addresses << i << ", " << update_distances[i].size() << ", " << max_distance << ", " << avg << ", " << update_distances[i].front() << ", " << update_distances[i].back() << ", " << avg * update_distances[i].size() << endl;
			}
			skew[update_distances[i].size()]++;
			averages[update_distances[i].size()] += avg;
		}
	}
	addresses_stream_init_addresses.close();
	addresses_stream_added_addresses.close();

	for(auto outer_iter = skew.begin(); outer_iter != skew.end(); ++outer_iter) {
		int key = (*outer_iter).first;
		int num = (*outer_iter).second;
		averages[key] /= num;
		cout << key << "\t" << num << "\t" << (long)averages[key] << endl;
	}

	file.close();
}

void File_Reading_Thread::print_trace_stats() {
	string line = "";
	map<int, int> count;
	const int window_size = 100000;
	int window_num = 0;
	vector<int> log_writes;
	int consecutive_writes = 0;
	vector<int> total_writes;
	const int stop_after_how_many_windows = 5;
	bool in_initialization;
	const bool do_not_consider_initialization = in_initialization = true;

	ofstream addresses_stream;
	addresses_stream.open("/home/niv/Desktop/EagleTree/changing_workload/pure_trace.csv");
	int counter = 0;
	while (getline(file,line)) {
		int num_log_writes = std::count(line.begin(), line.end(), 'l');
		string number = line;
		if (num_log_writes > 0) {
			number = number.substr(num_log_writes,number.size());
		}
		int addr = atoi(number.c_str());
		bool skip = in_initialization && do_not_consider_initialization;
		if (addr != 0 && !skip) {
			if (num_log_writes > 0) {
				log_writes.push_back(num_log_writes);
				total_writes.push_back(consecutive_writes);
				consecutive_writes = 0;
			}
			else {
				consecutive_writes++;
			}

			//printf("%d\n", addr);

			count[addr]++;
			unique_addresses.insert(addr);
			//trace.push_back(addr);
			//printf("%d\n", addr);
			if (io_num % window_size == 0) {
				logical_address_space_size_over_time.push_back(unique_addresses.size());
				window_num++;
			}
			if (window_num == stop_after_how_many_windows) {
				break;
			}
			if (addr > highest_address) {
				highest_address = addr;
			}
			addresses_stream << counter++ << ", " << addr << endl;
			in_initialization = false;
		}
		if (in_initialization && line.find("Loading finished") != string::npos) {
			in_initialization = false;
			printf("finished with init phase.\n");
			printf("Num unique addresses in init phase is %d %d\n", initial_addresses.size());
			printf("Num writes in init phase is %d  %d\n", io_num);
			io_num = 0;

		}
		else if (in_initialization) {
			count[addr]++;
			initial_addresses.insert(addr);
			unique_addresses.insert(addr);
		}
		io_num++;
	}
	addresses_stream.close();
	int total_log_writes = 0;
	for (auto i : log_writes) {
		total_log_writes += i;
	}

	printf("logical address space: %d      highest address: %d\n", unique_addresses.size(), highest_address);
	printf("Num unique addresses in init phase is %d %d\n", initial_addresses.size());
	printf("num writes: %d\n", io_num);
	printf("total_log_writes: %d\n", total_log_writes);
	printf("consecutive log writes: %d   consecutive data writes: %d\n", log_writes.size(), total_writes.size());
	if (log_writes.size() != 0 && total_writes.size() != 0) {
		printf("avg log writes per consec: %d   avg data writes per consec: %d\n", total_log_writes / log_writes.size() , io_num /  total_writes.size());
	}
	printf("size growth over time\n");
	for (int i = 1; i < logical_address_space_size_over_time.size(); i++) {
		int diff = logical_address_space_size_over_time[i] - logical_address_space_size_over_time[i-1];
		//printf("\t%d:\t%d\n", window_size * (i - 1), diff);
		printf("\t%d\n", diff);
	}

	ofstream output;
	output.open ("/home/niv/Desktop/EagleTree/changing_workload/addr_freq.csv");
	map<int, int> meta_count;
	for (auto a : count) {
		if (initial_addresses.count(a.first) == 0) {
			meta_count[a.second]++;
			if (a.second == 1) {

			}
			if (a.first == 757081) {
				printf("%d, %d\n", a.first, a.second);
			}
			//printf("%d, %d\n", a.first, a.second);
			output << a.first << "," << a.second << endl;
		}

	}
	output.close();

	printf("frequency map\n");
	for (auto a : meta_count) {
		printf("%d\t%d\n", a.first, a.second);
	}
	unique_addresses.clear();
	file.close();
}

File_Reading_Thread::~File_Reading_Thread() {
	file.close();
}

void File_Reading_Thread::issue_first_IOs() {
	file.open(file_name.c_str());
	read_and_submit();
}

void File_Reading_Thread::read_and_submit() {
	if (!file.is_open()) return;
	string line = "";
	static int line_counter = 0;
	//MAX_SSD_QUEUE_SIZE = 1;
	//PRINT_LEVEL = 1;
	int addr = UNDEFINED;
	int counter = 100;
	while (get_num_ongoing_IOs() < MAX_SSD_QUEUE_SIZE && !is_finished() && !is_stopped() &&  (addr = get_next()) != -10000) {

		if (addr >= 0) {
			if (addr >= address_map.size()) {
				continue;
			}
			int translated_addr = address_map[addr];
			if (translated_addr == UNDEFINED) {
				//printf("not ")
				continue;
			}
			if (translated_addr > NUMBER_OF_ADDRESSABLE_PAGES() * OVER_PROVISIONING_FACTOR) {
				printf("translated_addr too high:   %d  %d\n", addr, translated_addr);
			}
			int tag = update_frequency_count[addr] / 10;
			assert(translated_addr != UNDEFINED );
			//printf("%d\n", addr);
			Event* e = new Event(WRITE, translated_addr, 1, get_time());
			if (tag == 4) {
				tag = 5;
			}
			else if (tag == 6) {
				tag = 7;
			}
			e->set_tag(tag);
			/*if (tag == 2) {
				static set<int> in_group_2;
				in_group_2.insert(addr);
				printf("in group 2:   %d   %d    %d\n", addr, update_frequency_count[addr], in_group_2.size());
			}*/
			submit(e);
		}
	}
	if (addr == -10000) {
		//printf("calling get_back_to_start_after_init   !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
		//get_back_to_start_after_init();
		//read_and_submit();
	}
}

void File_Reading_Thread::handle_event_completion(Event* event) {
	read_and_submit();
}

