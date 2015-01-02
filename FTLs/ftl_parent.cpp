#include "../ssd.h"
using namespace ssd;


FtlParent::FtlParent(Ssd *ssd, Block_manager_parent* bm)
: ssd(ssd), scheduler(NULL), bm(bm), normal_stats("ftl_stats", 50000) {

}

FtlParent::~FtlParent () {
	if (normal_stats.num_mapping_reads > 0 || normal_stats.num_mapping_writes > 0) {
		normal_stats.print();
	}
}

FtlParent::stats::stats(string name, long counter_limit) : num_mapping_reads(0), num_mapping_writes(0),
		mapping_reads_per_interval(0), mapping_writes_per_interval(0),
		gc_reads_per_interval(0), gc_writes_per_interval(0),
		app_reads_per_interval(0), app_writes_per_interval(0),
		counter(0), num_noop_reads_per_interval(0), num_noop_writes_per_interval(0), file_name(name),
		COUNTER_LIMIT(counter_limit), gc_mapping_reads_per_interval(0), gc_mapping_writes_per_interval(0),
		num_noop_reads(0), num_noop_writes(0)
{}

void FtlParent::stats::collect_stats(Event const& event) {
	/*if (set2.size() > 0) {
		printf("set2 %d\n", set2.size());
	}*/
	if (event.get_noop() && event.get_event_type() == WRITE) {
		num_noop_writes++;
	} else if (event.get_noop() && event.get_event_type() == READ_TRANSFER) {
		num_noop_reads++;
	} else if (event.is_mapping_op() && event.is_garbage_collection_op() && event.get_event_type() == WRITE) {
		gc_mapping_writes_per_interval++;
		//set1.insert(event.get_application_io_id());
	} else if (event.is_mapping_op() && event.is_garbage_collection_op() && event.get_event_type() == READ_TRANSFER) {
		gc_mapping_reads_per_interval++;
	}else if (event.is_original_application_io() && event.get_event_type() == WRITE) {
		app_writes_per_interval++;
		counter++;
	} else if (event.is_original_application_io() && event.get_event_type() == READ_TRANSFER) {
		app_reads_per_interval++;
	} else if (event.is_garbage_collection_op() && event.get_event_type() == WRITE) {
		gc_writes_per_interval++;
		//set1.insert(event.get_application_io_id());
	} else if (event.is_garbage_collection_op() && event.get_event_type() == READ_TRANSFER) {
		gc_reads_per_interval++;
	} else if (event.is_mapping_op() && event.get_event_type() == WRITE) {
		mapping_writes_per_interval++;
		num_mapping_writes++;
	} else if (event.is_mapping_op() && event.get_event_type() == READ_TRANSFER) {
		mapping_reads_per_interval++;
		num_mapping_reads++;
	}

	if (counter >= COUNTER_LIMIT) {
		counter = 0;

		StatisticData::register_statistic(file_name, {
				new Integer(StatisticsGatherer::get_global_instance()->total_writes()),
				new Integer(gc_mapping_reads_per_interval),
				new Integer(gc_mapping_writes_per_interval),
				new Integer(mapping_reads_per_interval),
				new Integer(mapping_writes_per_interval),
				new Integer(gc_reads_per_interval),
				new Integer(gc_writes_per_interval),
				new Integer(app_reads_per_interval),
				new Integer(app_writes_per_interval),
		});

		StatisticData::register_field_names(file_name, {
				"writes",
				"gc_mapping_reads_per_interval",
				"gc_mapping_writes_per_interval",
				"mapping_reads_per_interval",
				"mapping_writes_per_interval",
				"gc_reads_per_interval",
				"gc_writes_per_interval",
				"app_reads_per_interval",
				"app_writes_per_interval"
		});

		gc_mapping_reads_per_interval = 0;
		gc_mapping_writes_per_interval = 0;
		mapping_reads_per_interval = 0;
		mapping_writes_per_interval = 0;
		gc_reads_per_interval = 0;
		gc_writes_per_interval = 0;
		app_reads_per_interval = 0;
		app_writes_per_interval = 0;
	}
}

void FtlParent::collect_stats(Event const& event) {
	normal_stats.collect_stats(event);
}

void FtlParent::stats::print() const {
	printf("FTL mapping reads\t%d\n", num_mapping_reads);
	printf("FTL mapping writes\t%d\n", num_mapping_writes);
	printf("FTL noop writes\t%d\n", num_noop_writes);
	printf("FTL noop reads\t%d\n", num_noop_reads);
}
