#ifndef INETWORK_H
#define INETWORK_H

#include <string>
#include <functional>

class INetwork
{
public:
    virtual ~INetwork() {}
    virtual void send(const std::string &message, const std::string &destAddr, int destPort) = 0;
    virtual void startReceiving(std::function<void(const std::string &)> onReceive) = 0;
};

#endif // INETWORK_H