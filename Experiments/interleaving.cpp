#include "../ssd.h"
#include <unistd.h>   // chdir
#include <sys/stat.h> // mkdir

using namespace ssd;

//BOOST_CLASS_EXPORT_GUID(FtlParent, "FtlParent")
//BOOST_CLASS_EXPORT_GUID(FtlImpl_Page, "FtlImpl_Page")

int main()
{
	set_normal_config();
	string exp_folder  = "interleaving_exp/";
 	mkdir(exp_folder.c_str(), 0755);

 	Workload_Definition* init = new Init_Workload();
	Workload_Definition* workload = new Asynch_Random_Workload();
	int IO_limit = 200000;
	double op_min = 0.60;
	double op_max = 0.60;
	double incr = 0.1;
	vector<vector<ExperimentResult> > exps;

	SCHEDULING_SCHEME = 0;
	ALLOW_DEFERRING_TRANSFERS = false;
	exps.push_back( Experiment_Runner::simple_experiment(workload, init,	exp_folder + "no_split/", "no split", IO_limit, OVER_PROVISIONING_FACTOR, op_min, op_max, incr) );

	SCHEDULING_SCHEME = 0;
	ALLOW_DEFERRING_TRANSFERS = true;
	exps.push_back( Experiment_Runner::simple_experiment(workload, init,	exp_folder + "split/", "split", IO_limit, OVER_PROVISIONING_FACTOR, op_min, op_max, incr) );

	SCHEDULING_SCHEME = 1;
	ALLOW_DEFERRING_TRANSFERS = true;
	//exps.push_back( Experiment_Runner::simple_experiment(workload, init,	exp_folder + "smart/", "smart", IO_limit, OVER_PROVISIONING_FACTOR, op_min, op_max, incr) );

	//SCHEDULING_SCHEME = 4;
	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "fifo/", "fifo", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );
	//SCHEDULING_SCHEME = 0;
	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "all_equal/", "all_equal", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );

	//exps.push_back( Experiment_Runner::simple_experiment(workload,	exp_folder + "deadlines3333/", "deadlines3333/", IO_limit, WRITE_DEADLINE, deadline_min, deadline_max, incr) );
	delete init;
	delete workload;
	/*Experiment_Runner::draw_graphs(exps, exp_folder);
	vector<int> num_write_thread_values_to_show(0);
	for (int i = op_min; i <= op_max; i += incr)
		num_write_thread_values_to_show.push_back(i); // Show all used spaces values in multi-graphs
	Experiment_Runner::draw_experiment_spesific_graphs(exps, exp_folder, num_write_thread_values_to_show);
	chdir(".."); // Leaving*/
	return 0;
}



/*	Page* a = new B(44000, 64000);
	{
	  std::ofstream file("archive.xml");
	  boost::archive::text_oarchive oa(file);
	  oa.register_type<B>( );
	  oa << a;
	}

	A* loaded;
	B* dr2;
	{
	  std::ifstream file("archive.xml");
	  boost::archive::text_iarchive ia(file);
	  ia.register_type<B>( );
	  ia >> loaded;
	  dr2 = dynamic_cast<B*> (loaded);
	}
 */


//FtlImpl_Page otherftl = FtlImpl_Page(this);
/*FtlParent* a = new FtlImpl_Page();
{
  std::ofstream file("archive.xml");
  boost::archive::text_oarchive oa(file);
  oa.register_type<FtlImpl_Page>( );
  oa << a;
}

FtlParent* loaded;
FtlImpl_Page* dr2;
{
  std::ifstream file("archive.xml");
  boost::archive::text_iarchive ia(file);
  ia.register_type<FtlImpl_Page>( );
  ia >> loaded;
  dr2 = dynamic_cast<FtlImpl_Page*> (loaded);
}*/
