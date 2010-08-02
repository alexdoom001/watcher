#!/bin/bash

usage()
{
	echo "
`basename $0` - watch for changes of files/directories and log them

OPTIONS:
  -r) recursive watch
  -h) this help
  -e <event>) event to watch

ALLOWED_EVENTS:
  $ALLOWED_EVENTS
"
}

ERROR() {
	echo "$@" >&2
	exit 1
}

ALLOWED_EVENTS="access modify attrib close_write close_nowrite close open \
moved_to moved_from move move_self create delete delete_self unmount"

DESCR_ACCESS="Access to"
DESCR_MODIFY="Modification of"
DESCR_ATTRIB="Attrs changed"
DESCR_CLOSE="Close"
DESCR_CLOSE_WRITE=$DESCR_CLOSE
DESCR_CLOSE_NOWRITE=$DESCR_CLOSE
DESCR_OPEN="Open"
DESCR_MOVE="Move"
DESCR_MOVED_TO=$DESCR_MOVE
DESCR_MOVED_FROM=$DESCR_MOVE
DESCR_CREATE="Create"
DESCR_DELETE="Delete"
DESCR_DELETE_SELF=$DESCR_DELETE
DESCR_UNMOUNT="Unmount"

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

which inotifywait >/dev/null || ERROR "inotifywait not found"

trap 'kill $inotify_pid 0' TERM KILL


#NOTE: WE HAVE NOT TO MONITOR SUCH PROGRAMS LIKE tr, logger, ... ON OPEN AND ACCESS.

inotifywait -m $RECURSIVE $EVENTS $@ | while read target events file; do
	for event in ${events//,/ }; do
		eval logger -t fam -p user.notice \$DESCR_${event} ${target}$file
	done
done &
inotify_pid=$!
wait
