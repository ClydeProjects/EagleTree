#include "../ssd.h"
using namespace ssd;

int main()
{
	printf("Running EagleTree\n");
	set_small_SSD_config();

	/*PAGE_READ_DELAY = 12;
	PAGE_WRITE_DELAY = 30;
	BUS_CTRL_DELAY = 1;
	BUS_DATA_DELAY = 10;
	BLOCK_ERASE_DELAY = 32;*/

	SSD_SIZE = 4;
	PACKAGE_SIZE = 2;
	GREED_SCALE = 4;
	BLOCK_MANAGER_ID = 7;
	BLOCK_SIZE = 128;
	PRINT_LEVEL = 0;
	//SCHEDULING_SCHEME = 0;
	OVER_PROVISIONING_FACTOR = 0.7;
	GARBAGE_COLLECTION_POLICY = 0;
	string name  = "/demo_output/";
	Experiment::create_base_folder(name.c_str());
	Experiment* e = new Experiment();

	Workload_Definition* workload = new Init_Workload();

	e->set_workload(workload);
	e->set_io_limit(NUMBER_OF_ADDRESSABLE_PAGES() * 2);
	e->run("test");

	//BLOCK_MANAGER_ID = 0;
	//e->run("test");
	e->draw_graphs();
	delete workload;
	return 0;
}
