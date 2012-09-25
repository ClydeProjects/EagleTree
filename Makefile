# Copyright 2009, 2010 Brendan Tauras

# Makefile is part of FlashSim.

# FlashSim is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# any later version.

# FlashSim is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with FlashSim.  If not, see <http://www.gnu.org/licenses/>.

##############################################################################

# FlashSim makefile
# Brendan Tauras 2010-08-03
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
CFLAGS = -Wall -Wextra -g -std=gnu++0x -O2
CXX = /usr/bin/g++
CXXFLAGS = $(CFLAGS)
ELF0 = run_test
ELF1 = run_trace
#ELF2 = test2
#ELF2 = run_test2
HDR = ssd.h 
VPATH = FTLs MTRand BlockManagers OperatingSystem Utilities
#SRC = ssd_address.cpp ssd_block.cpp ssd_bus.cpp ssd_channel.cpp ssd_config.cpp ssd_controller.cpp ssd_die.cpp ssd_event.cpp ssd_ftl.cpp ssd_gc.cpp ssd_package.cpp ssd_page.cpp ssd_plane.cpp ssd_quicksort.cpp ssd_ram.cpp ssd_ssd.cpp ssd_wl.cpp
#OBJ = ssd_address.o ssd_block.o ssd_bus.o ssd_channel.o ssd_config.o ssd_controller.o ssd_die.o ssd_event.o ssd_ftl.o ssd_gc.o ssd_package.o ssd_page.o ssd_plane.o ssd_quicksort.o ssd_ram.o ssd_ssd.o ssd_wl.o
SRC = page_ftl.cpp ssd_address.cpp ssd_block.cpp ssd_bus.cpp ssd_channel.cpp ssd_config.cpp ssd_controller.cpp ssd_die.cpp ssd_event.cpp ssd_package.cpp ssd_page.cpp ssd_plane.cpp ssd_ram.cpp ssd_ssd.cpp ssd_stats.cpp dftl_ftl.cpp ssd_ftlparent.cpp dftl_parent.cpp ssd_io_scheduler.cpp bm_shortest_queue.cpp ssd_page_hotness_measurer.cpp bm_wearwolf.cpp bm_wearwolf_locality.cpp  bm_hot_cold_seperation.cpp bm_parent.cpp ssd_visual_tracer.cpp ssd_state_visualiser.cpp ssd_statistics_gatherer.cpp operating_system.cpp thread_implementations.cpp class ssd_sequential_pattern_detector.cpp mtrand.cpp external_sort.cpp bm_round_robin.cpp File_Manager.cpp random_order_iterator.cpp experiment_runner.cpp
OBJ = page_ftl.o ssd_address.o ssd_block.o ssd_bus.o ssd_channel.o ssd_config.o ssd_controller.o ssd_die.o ssd_event.o ssd_package.o ssd_page.o ssd_plane.o ssd_ram.o ssd_ssd.o ssd_stats.o dftl_ftl.o ssd_ftlparent.o dftl_parent.o ssd_io_scheduler.o bm_shortest_queue.o ssd_page_hotness_measurer.o bm_wearwolf.o bm_wearwolf_locality.o bm_hot_cold_seperation.o bm_parent.o ssd_visual_tracer.o ssd_state_visualiser.o ssd_statistics_gatherer.o operating_system.o thread_implementations.o ssd_sequential_pattern_detector.o mtrand.o external_sort.o bm_round_robin.o File_Manager.o random_order_iterator.o experiment_runner.o
LOG = log
PERMS = 660
EPERMS = 770

ssd: $(HDR) $(SRC)
	$(CXX) $(CXXFLAGS) -c $(SRC)
	-chmod $(PERMS) $(OBJ)
#script -c "$(CXX) $(CXXFLAGS) -c $(SRC)" $(LOG)
#-chmod $(PERMS) $(LOG) $(OBJ)

# All Target
all: sequential copyback_map gc_tuning copybacks

copybacks: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o copybacks exp_copybacks.cpp $(OBJ)
	-chmod $(PERMS) $(OBJ)
	-chmod $(EPERMS) copybacks

copyback_map: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o copyback_map exp_copyback_map.cpp $(OBJ)
	-chmod $(PERMS) $(OBJ)
	-chmod $(EPERMS) copyback_map

gc_priorities: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o gc_priorities exp_gc_priorities.cpp $(OBJ)
	-chmod $(PERMS) $(OBJ)
	-chmod $(EPERMS) gc_priorities
	
gc_tuning: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o gc_tuning exp_gc_tuning.cpp $(OBJ)
	-chmod $(PERMS) $(OBJ)
	-chmod $(EPERMS) gc_tuning

sequential: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o sequential exp_sequential.cpp $(OBJ)
	-chmod $(PERMS) $(OBJ)
	-chmod $(EPERMS) sequential
	
load_balancing: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o load_balancing exp_load_balancing.cpp $(OBJ)
	-chmod $(PERMS) $(OBJ)
	-chmod $(EPERMS) load_balancing


exp1: $(HDR) $(OBJ)
	#$(CXX) $(CXXFLAGS) -I/home/niv/install/boost_1_47_0 -L/home/niv/install/boost_1_47_0/libs -o $(ELF2) run_exp1.cpp $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(ELF2) run_exp1.cpp $(OBJ)
	-chmod $(PERMS) $(OBJ)
	-chmod $(EPERMS) $(ELF2)
#script -c "$(CXX) $(CXXFLAGS) -o $(ELF2) run_exp1.cpp $(OBJ)" $(LOG)
#-chmod $(PERMS) $(LOG) $(OBJ)

trace: $(HDR) $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(ELF1) run_trace.cpp $(OBJ)
	-chmod $(PERMS) $(OBJ)
	-chmod $(EPERMS) $(ELF1)
#script -c "$(CXX) $(CXXFLAGS) -o $(ELF1) run_trace.cpp $(OBJ)" $(LOG)
#-chmod $(PERMS) $(LOG) $(OBJ)

clean:
	-rm -f $(OBJ) $(LOG) $(ELF0) $(ELF1) $(ELF2) erase_queues

files:
	echo $(SRC) $(HDR)
