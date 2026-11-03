#include "DNS_Server.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"

static const char *TAG = "dns";
static constexpr size_t HEADER_LEN = 12;
static constexpr size_t ANSWER_LEN = 16;
static constexpr uint16_t QTYPE_A = 1;

// Turns the query in buf into a response in place. Returns its length, or -1 to drop it.
static int build_response(uint8_t *buf, int len, size_t cap, uint32_t ip)
{
    if (len < (int)HEADER_LEN + 5)
    {
        return -1;
    }
    bool is_response = buf[2] & 0x80;
    uint8_t opcode = (buf[2] >> 3) & 0x0f;
    uint16_t questions = (buf[4] << 8) | buf[5];
    if (is_response || opcode != 0 || questions != 1)
    {
        return -1;
    }

    // Walk QNAME. Compression pointers are not valid in a single-question query.
    int pos = HEADER_LEN;
    while (pos < len && buf[pos] != 0)
    {
        if (buf[pos] & 0xc0)
        {
            return -1;
        }
        pos += buf[pos] + 1;
    }
    if (pos + 5 > len)
    {
        return -1;
    }
    uint16_t qtype = (buf[pos + 1] << 8) | buf[pos + 2];
    size_t question_end = pos + 5;
    bool answer = qtype == QTYPE_A;
    if (question_end + (answer ? ANSWER_LEN : 0) > cap)
    {
        return -1;
    }

    buf[2] = 0x84 | (buf[2] & 0x01); // response, authoritative, keep RD
    buf[3] = 0x00;                   // NOERROR
    buf[6] = 0;
    buf[7] = answer ? 1 : 0; // non-A queries (e.g. AAAA) get an empty answer
    memset(&buf[8], 0, 4);
    if (!answer)
    {
        return question_end;
    }

    uint8_t *a = buf + question_end;
    a[0] = 0xc0; // name: pointer to the question
    a[1] = HEADER_LEN;
    a[2] = 0;
    a[3] = QTYPE_A;
    a[4] = 0;
    a[5] = 1; // class IN
    a[6] = a[7] = a[8] = 0;
    a[9] = 60; // TTL: keep short, the portal disappears after the AP window
    a[10] = 0;
    a[11] = 4;
    memcpy(&a[12], &ip, 4);
    return question_end + ANSWER_LEN;
}

bool DNS_Server::start()
{
    if (m_running.exchange(true))
    {
        return true;
    }
    if (xTaskCreate(task, "dns", 3072, this, 5, NULL) != pdPASS)
    {
        m_running = false;
        return false;
    }
    return true;
}

void DNS_Server::stop()
{
    m_running = false; // the task notices within its receive timeout
}

void DNS_Server::task(void *arg)
{
    static_cast<DNS_Server *>(arg)->run();
    vTaskDelete(NULL);
}

void DNS_Server::run()
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "socket failed: %d", errno);
        m_running = false;
        return;
    }
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(53);
    timeval timeout = {.tv_sec = 1, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (bind(sock, (sockaddr *)&addr, sizeof(addr)) != 0)
    {
        ESP_LOGE(TAG, "bind failed: %d", errno);
        close(sock);
        m_running = false;
        return;
    }

    uint8_t buf[300];
    while (m_running)
    {
        sockaddr_in client = {};
        socklen_t client_len = sizeof(client);
        int len = recvfrom(sock, buf, sizeof(buf), 0, (sockaddr *)&client, &client_len);
        if (len <= 0)
        {
            continue;
        }
        int reply = build_response(buf, len, sizeof(buf), m_ip);
        if (reply > 0)
        {
            sendto(sock, buf, reply, 0, (sockaddr *)&client, client_len);
        }
    }
    close(sock);
}
