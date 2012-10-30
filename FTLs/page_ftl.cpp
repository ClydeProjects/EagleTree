/* Copyright 2011 Matias Bjørling */

/* page_ftl.cpp  */

/* FlashSim is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version. */

/* FlashSim is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. */

/* You should have received a copy of the GNU General Public License
 * along with FlashSim.  If not, see <http://www.gnu.org/licenses/>. */

/****************************************************************************/

/* Implements a very simple page-level FTL without merge */

#include <new>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "../ssd.h"

using namespace ssd;

FtlImpl_Page::FtlImpl_Page(Ssd &ssd):
	FtlParent(ssd),
	logical_to_physical_map(NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE, UNDEFINED),
	physical_to_logical_map(NUMBER_OF_ADDRESSABLE_BLOCKS() * BLOCK_SIZE, UNDEFINED)
{}

FtlImpl_Page::~FtlImpl_Page(void)
{}

Address FtlImpl_Page::get_physical_address(Event const& event) const {
	long virtual_address = event.get_logical_address();
	long phys_addr = logical_to_physical_map[virtual_address];
	return phys_addr == UNDEFINED ? Address() : Address(phys_addr, PAGE);
}

void FtlImpl_Page::read(Event *event)
{
	IOScheduler::instance()->schedule_event(event);
}

void FtlImpl_Page::write(Event *event)
{
	IOScheduler::instance()->schedule_event(event);
}

void FtlImpl_Page::trim(Event *event)
{
	IOScheduler::instance()->schedule_event(event);
}

void FtlImpl_Page::register_write_completion(Event const& event, enum status result) {
	long new_phys_addr = event.get_address().get_linear_address();

	long logi_addr = event.get_logical_address();
	logical_to_physical_map[logi_addr] = new_phys_addr;
	physical_to_logical_map[new_phys_addr] = logi_addr;

	if (event.get_replace_address().valid == PAGE) {
		long old_phys_addr = event.get_replace_address().get_linear_address();
		physical_to_logical_map[old_phys_addr] = UNDEFINED;
	}
}

void FtlImpl_Page::register_read_completion(Event const& event, enum status result) {

}

void FtlImpl_Page::register_trim_completion(Event & event) {
	long phys_addr = event.get_replace_address().get_linear_address();
	long logi_addr = event.get_logical_address();
	logical_to_physical_map[logi_addr] = UNDEFINED;
	physical_to_logical_map[phys_addr] = UNDEFINED;
}

long FtlImpl_Page::get_logical_address(uint physical_address) const {
	return physical_to_logical_map[physical_address];
}

Address FtlImpl_Page::get_physical_address(uint logical_address) const {
	long phys_addr = logical_to_physical_map[logical_address];
	return phys_addr == UNDEFINED ? Address() : Address(phys_addr, PAGE);
}

void FtlImpl_Page::set_replace_address(Event& event) const {
	Address target = get_physical_address(event);
	event.set_replace_address(target);
}

void FtlImpl_Page::set_read_address(Event& event) {
	Address target = get_physical_address(event);
	event.set_address(target);
}


