#!/bin/bash

usage()
{
	echo "
$0 - watch for changes of files/directories and log them

OPTIONS:
  -r) recursive watch
  -h) this help
  -e <event>) event to watch
"
}

ERROR() {
	echo "$@" >&2
	exit 1
}

ALLOWED_EVENTS="access modify attrib close_write close_nowrite close open \
moved_to moved_from move move_self create delete delete_self unmount"

args=`getopt -o hre: -- "$@"`
eval set -- "$args"

while [ "$1" != "--" ]; do
	case "$1" in
	-h) usage; exit 0;;
	-s) CHKSUMS=y;;
	-r) RECURSIVE=-r;;
	-e) echo " $ALLOWED_EVENTS " | grep -q " $2 " || ERROR "$2: unsupported event"
	    EVENTS="$EVENTS -e $2"; shift;;
	-x) EXECUTE="$2"; shift;;
	esac
	shift
done
shift

which inotifywait || ERROR "inotifywait not found"

#NOTE: WE HAVE NOT TO MONITOR SUCH PROGRAMS LIKE tr, logger, ... ON OPEN AND ACCESS.

inotifywait -m $RECURSIVE $EVENTS $@ | while read target events file; do
	logmsg="Found event $events for ${target}$file"
	logger -t watcher -p user.notice "$logmsg"
done
