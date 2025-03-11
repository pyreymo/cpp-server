#ifndef ITIMER_H
#define ITIMER_H

#include <functional>
#include <cstdint>

class ITimer
{
public:
    virtual ~ITimer() {}
    virtual void start(uint64_t timeout, uint64_t repeat, std::function<void()> callback) = 0;
    virtual void stop() = 0;
    void *data;
};

#endif // ITIMER_H