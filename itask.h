#ifndef ITASK_H
#define ITASK_H

#include <functional>
#include <chrono>

class INetwork;

class ITask
{
public:
    virtual ~ITask() {}
    virtual void execute(INetwork *network) = 0;
    virtual void suspend() = 0;
    virtual void cancel() = 0;
    virtual size_t getId() const = 0;
    virtual bool shouldRetry() const { return retryCount_ < maxRetries_; }
    virtual bool isPeriodic() const { return false; }
    virtual std::chrono::milliseconds getPeriod() const { return std::chrono::milliseconds(0); }
    virtual bool isCancelled() const { return isCancelled_; }
    virtual bool isSuspended() const { return isSuspended_; }

protected:
    int retryCount_ = 0;
    int maxRetries_ = 3;
    bool isSuspended_ = false;
    bool isCancelled_ = false;
};

#endif // ITASK_H