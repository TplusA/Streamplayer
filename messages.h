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

#ifndef MESSAGES_H
#define MESSAGES_H

#include <stdbool.h>
#include <syslog.h>

/*!
 * Whether or not to make use of syslog.
 */
void msg_enable_syslog(bool enable_syslog);

/*!
 * Emit error to stderr and syslog.
 *
 * \param error_code The current error code as stored in errno.
 * \param priority A log priority as expected by syslog(3).
 * \param error_format Format string followed by arguments.
 */
void msg_error(int error_code, int priority, const char *error_format, ...)
    __attribute__ ((format (printf, 3, 4)));

/*
 * Emit log informative message to stderr and syslog.
 */
void msg_info(const char *format_string, ...)
    __attribute__ ((format (printf, 1, 2)));

#endif /* !MESSAGES_H */
