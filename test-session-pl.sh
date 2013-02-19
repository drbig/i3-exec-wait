#!/bin/sh

./i3-exec-wait.pl urxvt
i3-msg split h
./i3-exec-wait.pl urxvt
i3-msg split v
./i3-exec-wait.pl urxvt
