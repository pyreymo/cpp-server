# 基于 Libuv 的异步事件循环与任务调度工程

该工程演示了如何使用 [libuv](http://libuv.org/) 实现一个异步事件循环、任务调度、线程池以及 UDP 网络通信。项目中通过定义抽象接口（如 IEventLoop、INetwork、ITask、ITimer 等）实现了解耦，提供了灵活的扩展能力。

## 项目特点

- **异步事件循环**  
  采用 libuv 实现事件循环，封装为 IEventLoop 接口，并提供了基于 libuv 的具体实现 LibuvEventLoop。

- **UDP 网络通信**  
  定义了 INetwork 接口，并通过 UDPServer 实现 UDP 数据包的发送与接收，支持异步网络操作。

- **任务调度与线程池**  
  利用 TaskScheduler 将任务（实现了 ITask 接口的 SendTask）调度到线程池中执行，同时结合计时器实现任务的定时与重试机制。

- **多线程处理**  
  基于 C++11 标准实现线程池（ThreadPool），保证任务能并发执行，提升程序效率。

- **灵活的扩展性**  
  各模块均采用接口抽象设计，可以方便地扩展和替换底层实现，比如替换网络协议或调整任务调度策略。

## 依赖关系

- [libuv](http://libuv.org/)：用于实现异步事件循环和网络 IO。
- C++11 及以上编译器。

## 编译与运行

1. **安装 libuv 库**  
   请先安装 libuv 库，确保系统中可以链接到 libuv。

2. **编译工程**  
   使用 CMake 调用支持 C++11 标准编译器进行编译：
   ```bash
   mkdir -p build
   cd build
   cmake ..
   make
   ```

3. **运行程序**  
   编译完成后运行生成的可执行文件：
   ```bash
   ./event_app
   ```

   程序启动后会创建两个 UDP 服务器（分别监听 8080 和 8081 端口），并通过任务调度器和线程池实现消息的发送和重试机制，控制台会打印出接收到的消息。

## 项目结构

- **ievent_loop.h / libuv_event_loop.h**  
  定义事件循环接口及其基于 libuv 的实现，包括定时器（LibuvTimer）。

- **inetwork.h / udp_server.h**  
  定义网络通信接口及 UDP 服务器实现，负责 UDP 数据包的发送与接收。

- **itask.h / send_task.h**  
  定义任务接口及发送任务实现，任务中封装了消息发送逻辑和相关状态控制。

- **task_scheduler.h**  
  任务调度器，通过计时器和线程池协调任务执行，并使用异步通知主线程执行网络操作。

- **thread_pool.h**  
  线程池实现，负责管理工作线程和任务队列，支持多任务并发执行。

- **thread_manager.h**  
  线程管理器，封装了 UDP 服务器的创建与消息调度，协调 TaskScheduler 和 ThreadPool 的工作。

- **main.cpp**  
  工程入口，演示如何初始化事件循环、线程池、任务调度器以及 UDP 服务器，并进行消息发送。

## 贡献

欢迎大家提交 Issue 和 PR，一起改进此工程。如果你有任何建议或问题，欢迎在 GitHub 上进行讨论。

## 许可证

该工程采用 [MIT 许可证](LICENSE)。
