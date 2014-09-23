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
void msg_info(const char *format_string, ...);

#endif /* !MESSAGES_H */
