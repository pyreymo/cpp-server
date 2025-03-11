#ifndef IEVENT_LOOP_H
#define IEVENT_LOOP_H

#include <uv.h>

class ITimer;

class IEventLoop
{
public:
    virtual ~IEventLoop() {}
    virtual void run() = 0;
    virtual void stop() = 0;
    virtual ITimer *createTimer() = 0;
    virtual uv_loop_t *getRawLoop() = 0;
};

#endif // IEVENT_LOOP_H