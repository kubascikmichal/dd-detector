#ifndef __DNS_SERVER_H__
#define __DNS_SERVER_H__

#include <stdint.h>
#include <atomic>

// Minimal captive-portal DNS: answers every A query with one fixed IPv4 address.
class DNS_Server
{
public:
    // ip is in network byte order.
    explicit DNS_Server(uint32_t ip) : m_ip(ip) {}
    bool start();
    void stop();

private:
    static void task(void *arg);
    void run();

    uint32_t m_ip;
    std::atomic<bool> m_running{false};
};

#endif
