#!/bin/sh
DISPLAY=:0.0

# Set to an empty workspace
i3-msg workspace 4

./i3-exec-wait-hacks urxvt
./i3-exec-wait-hacks gnumeric

i3-msg focus left
i3-msg focus left
i3-msg split v

# We exit here, no need to do rest 
# if the focus and/or split is wrong
# at this point.
exit

./i3-exec-wait-hacks emacs
i3-msg "split h; layout tabbed"
./i3-exec-wait-hacks vlc
./i3-exec-wait-hacks thunar
i3-msg "resize shrink height 10 px or 10 ppt; focus up"
i3-msg "split h; layout tabbed"
./i3-exec-wait-hacks urxvt 
./i3-exec-wait-hacks urxvt
./i3-exec-wait-hacks urxvt
i3-msg "focus right; split h; layout tabbed"
./i3-exec-wait-hacks vlc
./i3-exec-wait-hacks gnumeric
./i3-exec-wait-hacks urxvt
i3-msg split v
./i3-exec-wait-hacks gthumb
i3-msg "resize shrink height 10 px or 10 ppt; split h"
