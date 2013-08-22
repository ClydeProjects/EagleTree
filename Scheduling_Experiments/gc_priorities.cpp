#include "../ssd.h"
using namespace ssd;

int main()
{
	set_small_SSD_config();
	WRITE_DEADLINE = 1000;
	Workload_Definition* workload = new Asynch_Random_Workload(0, NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE * 0.8);
	int IO_limit = 100000;
	SCHEDULING_SCHEME = 2;
	Experiment::simple_experiment(workload, "gc_priorities", IO_limit);

	SCHEDULING_SCHEME = 0;
	Experiment::simple_experiment(workload, "gc_priorities", IO_limit);
	delete workload;
}

