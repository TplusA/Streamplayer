#ifndef STREAMER_H
#define STREAMER_H

int streamer_setup(GMainLoop *loop);
void streamer_shutdown(GMainLoop *loop);

void streamer_start(void);
void streamer_stop(void);
void streamer_pause(void);
void streamer_next(void);

#endif /* !STREAMER_H */
