#ifndef LIBUV_EVENT_LOOP_H
#define LIBUV_EVENT_LOOP_H

#include "ievent_loop.h"
#include "itimer.h"
#include "log.h"
#include <uv.h>
#include <memory>

class LibuvTimer : public ITimer
{
public:
    LibuvTimer(uv_loop_t *loop) : loop_(loop)
    {
        uv_timer_init(loop, &timer_);
        timer_.data = this;
    }

    ~LibuvTimer()
    {
        uv_close((uv_handle_t *)&timer_, nullptr);
    }

    void start(uint64_t timeout, uint64_t repeat, std::function<void()> callback) override
    {
        callback_ = callback;
        uv_timer_start(&timer_, onTimer, timeout, repeat);
    }

    void stop() override
    {
        uv_timer_stop(&timer_);
    }

private:
    static void onTimer(uv_timer_t *handle)
    {
        LibuvTimer *timer = static_cast<LibuvTimer *>(handle->data);
        if (timer->callback_)
        {
            timer->callback_();
        }
    }

    uv_loop_t *loop_;
    uv_timer_t timer_;
    std::function<void()> callback_;
};

class LibuvEventLoop : public IEventLoop
{
public:
    LibuvEventLoop() { uv_loop_init(&loop_); }
    ~LibuvEventLoop() { uv_loop_close(&loop_); }
    void run() override { uv_run(&loop_, UV_RUN_DEFAULT); }
    void stop() override { uv_stop(&loop_); }
    ITimer *createTimer() override
    {
        return new LibuvTimer(&loop_);
    }
    uv_loop_t *getRawLoop() override { return &loop_; }

private:
    uv_loop_t loop_;
};

#endif // LIBUV_EVENT_LOOP_H