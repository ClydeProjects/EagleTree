
# FlashSim makefile
# 
# Use the "ssd" (default) make target for separate compilation to include
# with external programs, such as DiskSim or a custom drivers.
#
# Use the "test" make target to run the most basic test of your FTL scheme
# after adding your content to the FTL, wear-leveler, and garbage-collector
# classes.
# 
# Use the "trace" make target to run a more involved test of your FTL scheme
# after adding your content to the FTL, wear-leveler, and garbage-collector
# classes.  It is suggested to test with the "test" make target first.

CC = /usr/bin/gcc
CFLAGS = -std=c++0x -g #-Wextra -Wall
CXX = /usr/bin/g++
CXXFLAGS = $(CFLAGS)
ELF0 = run_test
ELF1 = run_trace
#ELF2 = test2
#ELF2 = run_test2
HDR = ssd.h block_management.h
VPATH = FTLs MTRand BlockManagers OperatingSystem Utilities Scheduler
SRC = OS_Schedulers.cpp Queue_Length_Statistics.cpp experiment_graphing.cpp experiment_result.cpp Individual_Threads_Statistics.cpp Migrator.cpp Free_Space_Meter.cpp Utilization_Meter.cpp Workload_Definitions.cpp Garbage_Collector.cpp Scheduling_Strategies.cpp events_queue.cpp wear_leveling_strategy.cpp grace_hash_join.cpp page_ftl.cpp address.cpp block.cpp config.cpp die.cpp event.cpp package.cpp page.cpp plane.cpp ram.cpp ssd.cpp scheduler.cpp bm_shortest_queue.cpp page_hotness_measurer.cpp bm_wearwolf.cpp bm_locality.cpp  bm_hot_cold_seperation.cpp bm_parent.cpp visual_tracer.cpp state_visualiser.cpp statistics_gatherer.cpp operating_system.cpp thread_implementations.cpp sequential_pattern_detector.cpp mtrand.cpp external_sort.cpp bm_round_robin.cpp File_Manager.cpp random_order_iterator.cpp experiment_runner.cpp flexible_reader.cpp
OBJ = OS_Schedulers.o Queue_Length_Statistics.o experiment_graphing.o experiment_result.o Individual_Threads_Statistics.o Migrator.o Free_Space_Meter.o Utilization_Meter.o Workload_Definitions.o Garbage_Collector.o Scheduling_Strategies.o events_queue.o wear_leveling_strategy.o grace_hash_join.o page_ftl.o address.o block.o config.o die.o event.o package.o page.o plane.o ram.o ssd.o scheduler.o bm_shortest_queue.o page_hotness_measurer.o bm_wearwolf.o bm_locality.o bm_hot_cold_seperation.o bm_parent.o visual_tracer.o state_visualiser.o statistics_gatherer.o operating_system.o thread_implementations.o sequential_pattern_detector.o mtrand.o external_sort.o bm_round_robin.o File_Manager.o random_order_iterator.o experiment_runner.o flexible_reader.o
LOG = log
PERMS = 660
EPERMS = 770

ssd: $(HDR) $(SRC)
	$(CXX) $(CXXFLAGS) -c $(SRC)
	-chmod $(PERMS) $(OBJ)
#script -c "$(CXX) $(CXXFLAGS) -c $(SRC)" $(LOG)
#-chmod $(PERMS) $(LOG) $(OBJ)

# All Target

all: interleaving  #scheduling sequential_tuning sequential greediness copybacks

interleaving: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o Experiments/interleaving Experiments/interleaving.cpp $(OBJ) -lboost_serialization
	-chmod $(PERMS) $(OBJ) 
	-chmod $(EPERMS) Experiments/interleaving

overprov: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o Experiments/overprov Experiments/over_prov.cpp $(OBJ) -lboost_serialization
	-chmod $(PERMS) $(OBJ) 
	-chmod $(EPERMS) Experiments/overprov

sequential: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o Experiments/sequential Experiments/sequential.cpp $(OBJ) -lboost_serialization
	-chmod $(PERMS) $(OBJ)
	-chmod $(EPERMS) sequential

deadlines: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o Experiments/deadlines Experiments/deadlines.cpp $(OBJ) -lboost_serialization
	-chmod $(PERMS) $(OBJ) 
	-chmod $(EPERMS) Experiments/deadlines

balanced_sched: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o balanced_sched exp_balanced_scheduler.cpp $(OBJ) -lboost_serialization
	-chmod $(PERMS) $(OBJ)
	-chmod $(EPERMS) balanced_sched

run_test: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o run_test run_test3.cpp $(OBJ) -lboost_serialization
	-chmod $(PERMS) $(OBJ)
	-chmod $(EPERMS) run_test

flexible_reads: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o flexible_reads exp_flexible_reads.cpp $(OBJ) -lboost_serialization
	-chmod $(PERMS) $(OBJ)
	-chmod $(EPERMS) flexible_reads

grace: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o grace exp_grace_hash_join.cpp $(OBJ)
	-chmod $(PERMS) $(OBJ)
	-chmod $(EPERMS) grace

greediness: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o greediness exp_gc_greediness.cpp $(OBJ)
	-chmod $(PERMS) $(OBJ)
	-chmod $(EPERMS) greediness

scheduling: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o scheduling exp_scheduling_policies.cpp $(OBJ)
	-chmod $(PERMS) $(OBJ)
	-chmod $(EPERMS) scheduling
	
sequential_tuning: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o sequential_tuning exp_tuning_for_sequential.cpp $(OBJ)
	-chmod $(PERMS) $(OBJ)
	-chmod $(EPERMS) sequential_tuning

erase_queues: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o erase_queues exp_erase_queues.cpp $(OBJ)
	-chmod $(PERMS) $(OBJ)
	-chmod $(EPERMS) erase_queues

copybacks: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o copybacks exp_copybacks.cpp $(OBJ)
	-chmod $(PERMS) $(OBJ)
	-chmod $(EPERMS) copybacks

copyback_map: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o copyback_map exp_copyback_map.cpp $(OBJ)
	-chmod $(PERMS) $(OBJ)
	-chmod $(EPERMS) copyback_map
	
gc_tuning: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o gc_tuning exp_gc_tuning.cpp $(OBJ)
	-chmod $(PERMS) $(OBJ)
	-chmod $(EPERMS) gc_tuning
	
load_balancing: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o load_balancing exp_load_balancing.cpp $(OBJ)
	-chmod $(PERMS) $(OBJ)
	-chmod $(EPERMS) load_balancing

clean:
	-rm -f $(OBJ) $(LOG) $(ELF0) $(ELF1) $(ELF2) sequential erase_queues gc_tuning scheduling sequential_tuning greediness copybacks copyback_map flexible_reads

rm_exec:
	-rm -f sequential erase_queues gc_tuning scheduling sequential_tuning greediness copybacks copyback_map flexible_reads


files:
	echo $(SRC) $(HDR)
