#include <new>
#include <assert.h>
#include <stdio.h>
#include "ssd.h"

using namespace ssd;

Package::Package(long physical_address):
	data(),
	currently_executing_operation_finish_time(0)
{
	for(uint i = 0; i < PACKAGE_SIZE; i++) {
		int a = physical_address + (DIE_SIZE * PLANE_SIZE * BLOCK_SIZE * i);
		Die p = Die(a);
		data.push_back(p);
	}
}

Package::Package():
	data(),
	currently_executing_operation_finish_time(0) {}

enum status Package::read(Event &event)
{
	assert(event.get_address().die < PACKAGE_SIZE && event.get_address().valid > PACKAGE);
	return data[event.get_address().die].read(event);
}

enum status Package::write(Event &event)
{
	assert(event.get_address().die < PACKAGE_SIZE && event.get_address().valid > PACKAGE);
	return data[event.get_address().die].write(event);
}

enum status Package::erase(Event &event)
{
	assert(event.get_address().die < PACKAGE_SIZE && event.get_address().valid > PACKAGE);
	enum status status = data[event.get_address().die].erase(event);
	return status;
}

enum status Package::lock(double start_time, double duration, Event& event) {
	assert(start_time >= 0.0);
	assert(duration >= 0.0);

	Utilization_Meter::register_event(currently_executing_operation_finish_time, duration, event, PACKAGE);

	if (currently_executing_operation_finish_time > event.get_current_time()) {
		VisualTracer::print_horizontally(300);
		event.print();
		printf("currently_executing_io_finish_time: %f     %f\n", currently_executing_operation_finish_time, event.get_current_time());
	}

	if (currently_executing_operation_finish_time > event.get_current_time() + 0.000001) {
		assert(false);
	}
	currently_executing_operation_finish_time = event.get_current_time() + duration;

	if (event.get_event_type() == READ_TRANSFER || event.get_event_type() == COPY_BACK) {
		Address adr = event.get_address();
		int last_read_application_io = data[adr.die].get_last_read_application_io();

		if (last_read_application_io == event.get_application_io_id()) {
			data[adr.die].clear_register();
		}
		else if (last_read_application_io == UNDEFINED) {
			fprintf(stderr, "Register was empty\n", __func__);
			assert(false);
		}
		else if (last_read_application_io != event.get_application_io_id()) {
			fprintf(stderr, "Data belonging to a different read was in the register:  %d\n", __func__, last_read_application_io);
			assert(false);
		} else {
			data[adr.die].clear_register();
		}
	}

	event.incr_execution_time(duration);

	return SUCCESS;
}
