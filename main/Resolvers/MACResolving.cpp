#include "MACResolving.h"

static int hexValue(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

bool parseMAC(const char *str, uint8_t *mac)
{
    if (str == nullptr)
    {
        return false;
    }
    for (int i = 0; i < 6; i++)
    {
        const char *part = str + i * 3;
        int high = hexValue(part[0]);
        int low = high < 0 ? -1 : hexValue(part[1]);
        if (low < 0)
        {
            return false;
        }
        char separator = part[2];
        if ((i < 5 && separator != ':') || (i == 5 && separator != '\0'))
        {
            return false;
        }
        mac[i] = (high << 4) | low;
    }
    return true;
}
