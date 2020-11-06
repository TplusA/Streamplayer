# Streamplayer daemon

## Copyright and contact

T+A Streamplayer is released under the terms of the GNU General Public License
version 2 or (at your option) any later version. See file <tt>COPYING</tt> for
licensing terms of the GNU General Public License version 2, or
<tt>COPYING.GPLv3</tt> for licensing terms of the GNU General Public License
version 3.

Contact:

    T+A elektroakustik GmbH & Co. KG
    Planckstrasse 11
    32052 Herford
    Germany

## Short description

The _streamplayer_ is a generic media player with only the most elementary
capabilities and control features. It implements a short queue to enable
gapless playback, status feedback is given as _D-Bus_ signals.

Playlists, favorites, preset, shuffle, repeat, etc. are supposed to be
implemented somewhere else.

## Communication with other system process

The _streamplayer_ is primarily controlled over _D-Bus_ messages for
manipulation of the URL FIFO and starting, stopping, or pausing playback. The
daemon does not react on most signals sent by _dcpd_ because these signals
merely represent user requests, not definitive commands. The _drcpd_ daemon
takes these requests, and depending on the user interface state, they may or
may not be turned into direct _D-Bus_ message sent to _streamplayer_.

If the playback status changes or the position in the currently played stream
has changed, the _streamplayer_ announces these events as _D-Bus_ signals.


# Stream management

All items the player is supposed to play, such as files or internet radio
streams, are collectively referred to as _streams_. Their URLs specify the
exact stream type (http, rtp, local file, etc.) and location.

An example for a local file stored on some USB mass storage device would be
<tt>file:///mnt/usb/p1/music/my_file.mp3</tt>.

## Stream URL FIFO

Before a stream can be played, it must be loaded into the URL FIFO. This is a
short queue used to tell the _streamplayer_ which streams are likely going to
be played next ("likely" because the user may always push the Previous button
or set a direct URL via phone app). The URLs in the queue can be prefetched and
preprocessed by the _streamplayer_ so that gapless playback of consecutive
streams becomes possible.

The first URL stored in the FIFO is not the one that is currently playing. When
_streamplayer_ starts to play a URL, it removes the first URL from the FIFO and
stores it internally.

The FIFO content is manipulated using the _D-Bus_ messages
<tt>de.tahifi.Streamplayer.URLFIFO.Clear</tt>,
<tt>de.tahifi.Streamplayer.URLFIFO.Next</tt>, and
<tt>de.tahifi.Streamplayer.URLFIFO.Push</tt>.

The <tt>Clear</tt> message removes all entries from the URL FIFO. This message
causes _streamplayer_ to completely forget about all streams, except the one
currently playing (if any). Clearing the URL FIFO therefore does not stop the
playback.

The <tt>Next</tt> message removes the first entry from the URL FIFO and, if a
URL was playing already, causes _streamplayer_ to start playing this removed
entry, if any. If the playback was stopped or paused, the player removes the
first entry from the URL FIFO and takes it as new current stream, but otherwise
the playback state is retained. If there is no next entry, i.e., the FIFO
becomes empty, then nothing happens and the playback state is retained.

The <tt>Push</tt> message adds one new entry to the end of the URL FIFO. The
entry comprises of a stream ID, a stream URL, a start position, and a stop
position including units. The stream ID can be freely chosen by the caller and
is reported in <tt>NowPlaying</tt> signals. A stop position of
<code>INT64_MAX</code> means that the stream should be played to its very end.
In case the URL FIFO is full, the <tt>Push</tt> message returns a corresponding
return code in one of its arguments.

> **Note:** Absolute start position for <tt>Push</tt> might be OK, but allowing
>     a relative stop position (i.e., a duration) instead of only an absolute
>     one might be useful to support.

In some scenarios it is necessary to keep the first URL FIFO entry in place,
but to replace the rest of the FIFO by new entries. For this reason, both the
<tt>Clear</tt> and <tt>Push</tt> methods take a parameter that instruct
_streamplayer_ to keep the first *n* entries in the URL FIFO untouched by the
operation (where *n* is frequently set to 0 or 1). To keep all entries during a
<tt>Push</tt> operation, *n* must be set to -1. A value of -2 can be used to
clear the URL FIFO (like *n* = 0) and to start playing the new stream
immediately in case the player is already in playing state (avoiding a separate
call of the <tt>Next</tt> method and thus corresponding race conditions).

To avoid race conditions and other headaches, there is explicitly no direct way
to find out the maximum FIFO size. Instead, the result of a
<tt>de.tahifi.Streamplayer.URLFIFO.Push</tt> message must be checked to avoid
overflowing the FIFO, and the content of the
<tt>de.tahifi.Streamplayer.Playback.NowPlaying</tt> signal must be examined to
keep the URL FIFO filled.

## Playback information (_D-Bus_ interface <tt></tt>)

Whenever a new stream URL starts playing or its playback speed is changed, the
<tt>de.tahifi.Streamplayer.Playback.NowPlaying</tt> signal is sent. It contains
the stream ID and URL, and a flag that tells whether or not the URL FIFO is
full. The flag allows _drcpd_ to determine whether or not it makes sense to try
and push the next stream URL.

Whenever a stream is stopped or paused, a corresponding
<tt>de.tahifi.Streamplayer.Playback.Stopped</tt> or
<tt>de.tahifi.Streamplayer.Playback.PauseState</tt> signal is sent.

For each second played (or on periodic time position update during fast
winding), a <tt>de.tahifi.Streamplayer.Playback.PositionChanged</tt> signal is
sent so that the user interface can update its time display.

## Stream meta data

Any meta data such as artist, title, album name, stream name are sent with the
<tt>de.tahifi.Streamplayer.Playback.MetaDataChanged</tt> signal. In case the
meta data changes during playback (as is frequently the case with many internet
radio stations), a <tt>de.tahifi.Streamplayer.Playback.MetaDataChanged</tt>
signal containing all meta data is sent again.

The signal is also emitted in pause state so that any changes in meta data
information sent from some internet radio station can be displayed even in
pause mode.

> **Note:** Cover art (pictures showing the album cover or optical medium) is
>     _not_ handled directly by _streamplayer_ since this is a topic complex
>     enough to separate it out. Any cover art data extracted directly from a
>     stream is handed over to the _covartd_ daemon for further processing.
>     Cover art stored on files (neither inside stream files nor adjacent to
>     them) is not considered and extracted automatically by _covartd_, which
>     reacts on the <tt>NowPlaying</tt> signal.

## Fast winding

The signals from _dcpd_ for starting and stopping fast forward/rewind and for
setting the speed factor are directly handled by the _streamplayer_. When the
playback speed or direction (because there is fast rewind) is changed, then a
<tt>de.tahifi.Streamplayer.Playback.SpeedChanged</tt> signal is sent, with the
corresponding positive or negative speed factor set.
