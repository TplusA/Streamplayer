/*
 * Copyright (C) 2015  T+A elektroakustik GmbH & Co. KG
 *
 * This file is part of T+A Streamplayer.
 *
 * T+A Streamplayer is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 3 as
 * published by the Free Software Foundation.
 *
 * T+A Streamplayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with T+A Streamplayer.  If not, see <http://www.gnu.org/licenses/>.
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "messages.h"

static bool use_syslog;

void msg_enable_syslog(bool enable_syslog)
{
    use_syslog = enable_syslog;
}

static void show_message(int error_code, int priority,
                         const char *format_string, va_list va)
{
    char buffer[512];
    size_t len = vsnprintf(buffer, sizeof(buffer), format_string, va);

    if(error_code != 0 && len < sizeof(buffer))
        snprintf(buffer + len, sizeof(buffer) - len,
                 " (%s)", strerror(error_code));

    if(use_syslog)
        syslog(priority, "%s", buffer);

    if(error_code == 0)
        fprintf(stderr, "Info: %s\n", buffer);
    else
        fprintf(stderr, "Error: %s\n", buffer);
}

void msg_error(int error_code, int priority, const char *error_format, ...)
{
    va_list va;

    va_start(va, error_format);
    show_message(error_code, priority, error_format, va);
    va_end(va);
}

void msg_info(const char *format_string, ...)
{
    va_list va;

    va_start(va, format_string);
    show_message(0, LOG_INFO, format_string, va);
    va_end(va);
}
