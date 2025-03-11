#include <uv.h>
#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include <sstream>
#include <chrono>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

class ThreadPool
{
public:
    ThreadPool(size_t numThreads)
    {
        for (size_t i = 0; i < numThreads; ++i)
        {
            workers_.emplace_back([this]
                                  {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                        if (stop_ && tasks_.empty()) {
                            return;
                        }
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                } });
        }
    }

    ~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        for (std::thread &worker : workers_)
        {
            worker.join();
        }
    }

    void enqueue(std::function<void()> task)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (stop_)
            {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }
            tasks_.emplace(std::move(task));
        }
        condition_.notify_one();
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable condition_;
    bool stop_ = false;
};

// 日志函数
void log(const std::string &message, const char *file, int line)
{
    std::ostringstream oss;
    oss << "[" << file << ":" << line << "] " << message;
    std::cout << oss.str() << std::endl;
}

// 任务接口
class ITask
{
public:
    virtual ~ITask() {}
    virtual void execute() = 0;
    virtual void suspend() = 0;
    virtual void cancel() = 0;
    virtual size_t getId() const = 0;
    virtual bool shouldRetry() const { return retryCount_ < maxRetries_; }
    // 添加到接口中，提供默认实现
    virtual bool isPeriodic() const { return false; }
    virtual std::chrono::milliseconds getPeriod() const { return std::chrono::milliseconds(0); }

protected:
    int retryCount_;
    int maxRetries_;
    bool isSuspended_;
    bool isCancelled_;

    ITask() : retryCount_(0), maxRetries_(3), isSuspended_(false), isCancelled_(false) {}
};

// 发送 UDP 报文的任务
class SendTask : public ITask
{
public:
    SendTask(uv_udp_t *udpHandle, const std::string &msg, const sockaddr_in &destAddr,
             bool isPeriodic = false, std::chrono::milliseconds period = std::chrono::milliseconds(1000))
        : udpHandle_(udpHandle), message_(msg), destAddr_(destAddr),
          isPeriodic_(isPeriodic), period_(period)
    {
        id_ = std::hash<std::string>()(msg + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    }

    void execute()
    {
        if (isCancelled_ || isSuspended_)
            return;

        uv_udp_send_t *req = new uv_udp_send_t;
        uv_buf_t buf = uv_buf_init(const_cast<char *>(message_.c_str()), message_.size());
        int r = uv_udp_send(req, udpHandle_, &buf, 1, (const struct sockaddr *)&destAddr_, onSend);
        if (r != 0)
        {
            retryCount_++;
            log("Failed to send message, retry " + std::to_string(retryCount_) + "/" + std::to_string(maxRetries_),
                __FILE__, __LINE__);
            delete req;
        }
        else
        {
            log("Message sent: " + message_, __FILE__, __LINE__);
        }
    }

    static void onSend(uv_udp_send_t *req, int status)
    {
        if (status != 0)
        {
            log("Send failed: " + std::string(uv_strerror(status)), __FILE__, __LINE__);
        }
        delete req;
    }

    void suspend() { isSuspended_ = true; }
    void cancel() { isCancelled_ = true; }
    size_t getId() const { return id_; }
    bool isPeriodic() const { return isPeriodic_; }
    std::chrono::milliseconds getPeriod() const { return period_; }

private:
    uv_udp_t *udpHandle_;
    std::string message_;
    sockaddr_in destAddr_;
    size_t id_;
    bool isPeriodic_;
    std::chrono::milliseconds period_;
};

// 任务调度器
class TaskScheduler
{
public:
    TaskScheduler(uv_loop_t *loop) : loop_(loop) {}

    void schedule(std::shared_ptr<ITask> task, int priority)
    {
        uv_timer_t *timer = new uv_timer_t;
        uv_timer_init(loop_, timer);
        TaskEntry *entry = new TaskEntry{priority, task, timer};
        timer->data = entry;

        // 立即执行任务，周期性任务会根据 period 重复
        uint64_t timeout = 0;
        uint64_t repeat = task->isPeriodic() ? task->getPeriod().count() : 0;
        uv_timer_start(timer, onTimer, timeout, repeat);
    }

private:
    struct TaskEntry
    {
        int priority;
        std::shared_ptr<ITask> task;
        uv_timer_t *timer;
    };

    uv_loop_t *loop_;

    static void onTimer(uv_timer_t *handle)
    {
        TaskEntry *entry = static_cast<TaskEntry *>(handle->data);
        std::shared_ptr<SendTask> task = std::dynamic_pointer_cast<SendTask>(entry->task);
        task->execute();

        // 如果任务不是周期性的，且需要重试
        if (!task->isPeriodic() && task->shouldRetry())
        {
            uv_timer_start(handle, onTimer, 1000, 0); // 1秒后重试
        }
        else if (!task->isPeriodic() && !task->shouldRetry())
        {
            uv_timer_stop(handle);
            delete entry;
            uv_close((uv_handle_t *)handle, onClose);
        }
        // 周期性任务由 libuv 自动重复，无需手动重新调度
    }

    static void onClose(uv_handle_t *handle)
    {
        delete handle;
    }
};

// UDP 服务器
class UDPServer
{
public:
    UDPServer(int port, uv_loop_t *loop, TaskScheduler *scheduler, ThreadPool *threadPool)
        : port_(port), loop_(loop), scheduler_(scheduler), threadPool_(threadPool)
    {
        uv_udp_init(loop_, &udpHandle_);
        sockaddr_in addr;
        uv_ip4_addr("0.0.0.0", port_, &addr);
        uv_udp_bind(&udpHandle_, (const struct sockaddr *)&addr, 0);
        udpHandle_.data = threadPool; // 存储线程池指针
        uv_udp_recv_start(&udpHandle_, onAlloc, onRecv);
    }

    ~UDPServer()
    {
        uv_udp_recv_stop(&udpHandle_);
        uv_close((uv_handle_t *)&udpHandle_, nullptr);
    }

    void send(const std::string &message, const std::string &destAddr, int destPort,
              bool isPeriodic = false, std::chrono::milliseconds period = std::chrono::milliseconds(1000))
    {
        sockaddr_in addr;
        uv_ip4_addr(destAddr.c_str(), destPort, &addr);
        std::shared_ptr<SendTask> task = std::make_shared<SendTask>(&udpHandle_, message, addr, isPeriodic, period);
        scheduler_->schedule(task, isPeriodic ? 1 : 0);
    }

private:
    int port_;
    uv_loop_t *loop_;
    TaskScheduler *scheduler_;
    ThreadPool *threadPool_;
    uv_udp_t udpHandle_;

    static void onAlloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
    {
        buf->base = new char[suggested_size];
        buf->len = suggested_size;
    }

    static void onRecv(uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf,
                       const struct sockaddr *addr, unsigned flags)
    {
        if (nread > 0)
        {
            std::string msg(buf->base, nread);
            ThreadPool *threadPool = static_cast<ThreadPool *>(handle->data);
            threadPool->enqueue([msg]
                                {
                // CPU 密集型操作：报文解析
                log("Received and parsed: " + msg, __FILE__, __LINE__); });
        }
        delete[] buf->base;
    }
};

// 线程管理器
class ThreadManager
{
public:
    ThreadManager(uv_loop_t *loop, TaskScheduler *scheduler, ThreadPool *threadPool)
        : loop_(loop), scheduler_(scheduler), threadPool_(threadPool) {}

    void createServer(int port)
    {
        std::unique_ptr<UDPServer> server(new UDPServer(port, loop_, scheduler_, threadPool_));
        servers.push_back(std::move(server));
    }

    void sendMessage(size_t serverIndex, const std::string &message, const std::string &destAddr, int destPort,
                     bool isPeriodic = false, std::chrono::milliseconds period = std::chrono::milliseconds(1000))
    {
        if (serverIndex < servers.size())
        {
            servers[serverIndex]->send(message, destAddr, destPort, isPeriodic, period);
        }
        else
        {
            log("Invalid server index", __FILE__, __LINE__);
        }
    }

private:
    uv_loop_t *loop_;
    TaskScheduler *scheduler_;
    ThreadPool *threadPool_;
    std::vector<std::unique_ptr<UDPServer>> servers;
};

// 主函数
int main()
{
    uv_loop_t *loop = uv_default_loop();
    TaskScheduler scheduler(loop);
    ThreadPool threadPool(4); // 创建 4 个工作线程
    ThreadManager manager(loop, &scheduler, &threadPool);

    manager.createServer(8080);
    manager.createServer(8081);

    manager.sendMessage(0, "Hello from 8080", "127.0.0.1", 8081, true, std::chrono::milliseconds(2000));
    manager.sendMessage(1, "Hello from 8081", "127.0.0.1", 8080);

    uv_run(loop, UV_RUN_DEFAULT);

    return 0;
}
