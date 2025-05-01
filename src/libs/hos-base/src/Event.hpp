#ifndef LIB_HOS_BASE_EVENT_HPP
#define LIB_HOS_BASE_EVENT_HPP

#include "Types.hpp"
#include "stdbool.h"
#include "stdatomic.h"

class SimpleEvent {
public:
    explicit SimpleEvent(u64 counter);

    bool decrement();

    void add(u64 val);
    void set(u64 val);

private:
    u64 counter {};
};

#endif