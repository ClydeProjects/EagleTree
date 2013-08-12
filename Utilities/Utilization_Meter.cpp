#include "../ssd.h"
using namespace ssd;

vector<double> Utilization_Meter::channel_used 		= vector<double>();
vector<double> Utilization_Meter::channel_unused 	= vector<double>();
vector<double> Utilization_Meter::LUNs_used 		= vector<double>();
vector<double> Utilization_Meter::LUNs_unused 		= vector<double>();

void Utilization_Meter::init() {
	Utilization_Meter::channel_used = vector<double>(SSD_SIZE, 0);
	Utilization_Meter::channel_unused = vector<double>(SSD_SIZE, 0);
	Utilization_Meter::LUNs_used = vector<double>(SSD_SIZE * PACKAGE_SIZE, 0);
	Utilization_Meter::LUNs_unused = vector<double>(SSD_SIZE * PACKAGE_SIZE, 0);
}

void Utilization_Meter::register_event(double prev_time, double duration, Event const& event, address_valid gran) {
	assert(gran == PACKAGE || gran == DIE);
	if (gran == PACKAGE) {
		int id = event.get_address().package;
		channel_used[id] += duration;
		double unused_time = event.get_current_time() - prev_time;
		//VisualTracer::print_horizontally(1000);
		if (unused_time > 0) {
			//VisualTracer::print_horizontally(1000);
			//event.print();
		}
		channel_unused[id] += unused_time;
		//double util = channel_used[id] / (channel_used[id] + channel_unused[id]);
		//printf("C%d\t%f\n", id, util);
	} else if (gran == DIE) {
		int id = event.get_address().package * PACKAGE_SIZE + event.get_address().die;
		LUNs_used[id] += duration;
		LUNs_unused[id] += event.get_current_time() - prev_time;
	}
}

void Utilization_Meter::print() {
	printf("Channel Utilization\n");
	for (uint i = 0; i < SSD_SIZE; i++) {
		double util = channel_used[i] / (channel_used[i] + channel_unused[i]);
		printf("C%d\t%f\n", i, util);
	}
	/*printf("\nLUN Utilization\n");
	for (uint i = 0; i < SSD_SIZE * PACKAGE_SIZE; i++) {
		double util = LUNs_used[i] / (LUNs_used[i] + LUNs_unused[i]);
		printf("L%d\t%f\n", i, util);
	}*/
}

double Utilization_Meter::get_avg_channel_utilization() {
	double avg = 0;
	for (uint i = 0; i < SSD_SIZE; i++) {
		avg += channel_used[i] / (channel_used[i] + channel_unused[i]);
	}
	return avg / SSD_SIZE;
}

double Utilization_Meter::get_channel_utilization(int package_id) {
	return channel_used[package_id] / (channel_used[package_id] + channel_unused[package_id]);
}

double Utilization_Meter::get_LUN_utilization(int lun_id) {
	return LUNs_used[lun_id] / (LUNs_used[lun_id] + LUNs_unused[lun_id]);
}

double Utilization_Meter::get_avg_LUN_utilization() {
	double avg = 0;
	for (uint i = 0; i < SSD_SIZE * PACKAGE_SIZE; i++) {
		avg += LUNs_used[i] / (LUNs_used[i] + LUNs_unused[i]);
	}
	return avg / (SSD_SIZE * PACKAGE_SIZE);
}
