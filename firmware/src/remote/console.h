#ifndef __CONSOLE_H
#define __CONSOLE_H
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

    void console_init();

    void console_poll_usb();

#ifdef __cplusplus
}
#endif

#endif