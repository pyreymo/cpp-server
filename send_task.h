#ifndef SEND_TASK_H
#define SEND_TASK_H

#include "itask.h"
#include "inetwork.h"
#include <string>
#include <chrono>
#include <functional>

class SendTask : public ITask
{
public:
    SendTask(const std::string &msg, const std::string &destAddr, int destPort,
             bool isPeriodic = false, std::chrono::milliseconds period = std::chrono::milliseconds(1000))
        : message_(msg), destAddr_(destAddr), destPort_(destPort),
          isPeriodic_(isPeriodic), period_(period)
    {
        id_ = std::hash<std::string>()(msg + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    }

    void execute(INetwork *network) override
    {
        if (isCancelled_ || isSuspended_)
            return;
        network->send(message_, destAddr_, destPort_);
    }

    void suspend() override { isSuspended_ = true; }
    void cancel() override { isCancelled_ = true; }
    size_t getId() const override { return id_; }
    bool isPeriodic() const override { return isPeriodic_; }
    std::chrono::milliseconds getPeriod() const override { return period_; }

private:
    std::string message_;
    std::string destAddr_;
    int destPort_;
    size_t id_;
    bool isPeriodic_;
    std::chrono::milliseconds period_;
};

#endif // SEND_TASK_H