#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <functional>

#include "logic/interval_revelator.h"

class scheduler_t {   
public:
    interval_revelator_t on_frame_start;
};

#endif
