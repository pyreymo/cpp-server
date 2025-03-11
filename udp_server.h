#ifndef UDP_SERVER_H
#define UDP_SERVER_H

#include "inetwork.h"
#include "ievent_loop.h"
#include "log.h"
#include <uv.h>
#include <memory>
#include <functional>

class UDPServer : public INetwork
{
public:
    UDPServer(IEventLoop *eventLoop, int port) : eventLoop_(eventLoop)
    {
        uv_loop_t *loop = eventLoop->getRawLoop();
        uv_udp_init(loop, &udpHandle_);
        sockaddr_in addr;
        uv_ip4_addr("0.0.0.0", port, &addr);
        uv_udp_bind(&udpHandle_, (const struct sockaddr *)&addr, 0);
        udpHandle_.data = this;
    }

    ~UDPServer()
    {
        uv_close((uv_handle_t *)&udpHandle_, nullptr);
    }

    void send(const std::string &message, const std::string &destAddr, int destPort) override
    {
        uv_udp_send_t *req = new uv_udp_send_t;
        uv_buf_t buf = uv_buf_init(const_cast<char *>(message.c_str()), message.size());
        sockaddr_in dest;
        uv_ip4_addr(destAddr.c_str(), destPort, &dest);
        req->data = this;
        int r = uv_udp_send(req, &udpHandle_, &buf, 1, (const struct sockaddr *)&dest, onSend);
        if (r != 0)
        {
            log("Failed to send message: " + std::string(uv_strerror(r)), __FILE__, __LINE__);
            delete req;
        }
    }

    void startReceiving(std::function<void(const std::string &)> onReceive) override
    {
        onReceiveCallback_ = onReceive;
        uv_udp_recv_start(&udpHandle_, onAlloc, onRecv);
    }

private:
    static void onSend(uv_udp_send_t *req, int status)
    {
        if (status != 0)
        {
            log("Send failed: " + std::string(uv_strerror(status)), __FILE__, __LINE__);
        }
        delete req;
    }

    static void onAlloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
    {
        buf->base = new char[suggested_size];
        buf->len = suggested_size;
    }

    static void onRecv(uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf,
                       const struct sockaddr *addr, unsigned flags)
    {
        UDPServer *server = static_cast<UDPServer *>(handle->data);
        if (nread > 0 && server->onReceiveCallback_)
        {
            std::string msg(buf->base, nread);
            server->onReceiveCallback_(msg);
        }
        delete[] buf->base;
    }

    IEventLoop *eventLoop_;
    uv_udp_t udpHandle_;
    std::function<void(const std::string &)> onReceiveCallback_;
};

#endif // UDP_SERVER_H