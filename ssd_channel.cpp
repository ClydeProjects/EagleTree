
#include <new>
#include <assert.h>
#include <stdio.h>
#include <algorithm>
#include <stdexcept>
#include "ssd.h"

using namespace ssd;

Channel::Channel(Ssd* ssd, double ctrl_delay, double data_delay, uint table_size, uint max_connections):
	currently_executing_operation_finish_time(0.0),
	ssd(ssd)
{}

Channel::~Channel(void) {}

enum status Channel::lock(double start_time, double duration, Event& event) {
	assert(start_time >= 0.0);
	assert(duration >= 0.0);

	if (currently_executing_operation_finish_time > event.get_current_time() + 0.000001) {
		assert(false);
	}
	currently_executing_operation_finish_time = event.get_current_time() + duration;

	if (event.get_event_type() == READ_TRANSFER || event.get_event_type() == COPY_BACK) {
		Address adr = event.get_address();
		int last_read_application_io = ssd->getPackages()[adr.package].getDies()[adr.die].get_last_read_application_io();

		if (last_read_application_io == event.get_application_io_id()) {
			ssd->getPackages()[adr.package].getDies()[adr.die].clear_register();
		}
		else if (last_read_application_io == UNDEFINED) {
			fprintf(stderr, "Register was empty\n", __func__);
			assert(false);
		}
		else if (last_read_application_io != event.get_application_io_id()) {
			fprintf(stderr, "Data belonging to a different read was in the register:  %d\n", __func__, last_read_application_io);
			assert(false);
		} else {
			ssd->getPackages()[adr.package].getDies()[adr.die].clear_register();
		}
	}

	event.incr_execution_time(duration);

	return SUCCESS;
}

double Channel::get_currently_executing_operation_finish_time() {
	return currently_executing_operation_finish_time;
}

