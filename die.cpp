#include <new>
#include <assert.h>
#include <stdio.h>
#include "ssd.h"

using namespace ssd;

Die::Die(long physical_address):
	data(),
	currently_executing_io_finish_time(0.0),
	last_read_io(UNDEFINED)
{
	for(uint i = 0; i < DIE_SIZE; i++) {
		int a = physical_address + (PLANE_SIZE * BLOCK_SIZE * i);
		Plane p = Plane(a);
		data.push_back(p);
	}
}

Die::Die() :
	data(),
	currently_executing_io_finish_time(0.0),
	last_read_io(UNDEFINED) {}

enum status Die::read(Event &event)
{
	if (currently_executing_io_finish_time > event.get_current_time()) {
		VisualTracer::print_horizontally(500);
		event.print();
		printf("currently_executing_io_finish_time: %f     %f\n", currently_executing_io_finish_time, event.get_current_time());
	}
	assert(currently_executing_io_finish_time <= event.get_current_time());
	if (event.get_event_type() == READ_COMMAND) {
		last_read_io = event.get_application_io_id();
	}
	enum status result = data[event.get_address().plane].read(event);
	Utilization_Meter::register_event(currently_executing_io_finish_time, event.get_execution_time(), event, DIE);
	currently_executing_io_finish_time = event.get_current_time();
	return result;
}

enum status Die::write(Event &event)
{
	assert(currently_executing_io_finish_time <= event.get_current_time());
	enum status result = data[event.get_address().plane].write(event);
	Utilization_Meter::register_event(currently_executing_io_finish_time, event.get_execution_time(), event, DIE);
	currently_executing_io_finish_time = event.get_current_time();
	return result;
}

enum status Die::erase(Event &event)
{
	assert(currently_executing_io_finish_time <= event.get_current_time());
	enum status status = data[event.get_address().plane].erase(event);
	Utilization_Meter::register_event(currently_executing_io_finish_time, event.get_execution_time(), event, DIE);
	currently_executing_io_finish_time = event.get_current_time();
	return status;
}

double Die::get_currently_executing_io_finish_time() {
	return currently_executing_io_finish_time;
}

int Die::get_last_read_application_io() {
	return last_read_io;
}

bool Die::register_is_busy() {
	return last_read_io != -1;
}

void Die::clear_register() {
	last_read_io = -1;
}
