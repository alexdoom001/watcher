#include <stdio.h>
#include <errno.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <sys/reboot.h>
#include <linux/reboot.h>
#include <linux/limits.h>

char *progname = "watcher";

void usage(void) {
	printf("\n\
%s - report events on watched files to syslog and compute checksums. \n\
\n\
%s [-i] <chksums file>\n\
\n\
-i - make instant poweroff, do not try to go through init 0.\n\
", progname, progname);
}

#define BUFSIZE (sizeof(struct inotify_event) + 2*PATH_MAX + 32)

int events_to_watch = IN_DELETE_SELF | IN_MOVE_SELF | IN_OPEN | IN_MODIFY;
struct emap {
	uint32_t mask;
	char *name;
};
struct emap events[] = {
	{IN_DELETE_SELF, "Delete"},
	{IN_MOVE_SELF, "Move"},
	{IN_OPEN, "Open"},
	{IN_MODIFY, "Modify"},
	{0, NULL},
};
struct flist {
	char *name;
	int wd;
	struct flist *next;
};

#define xerror(...) ({ \
	fprintf(stderr, "%s: ", progname); \
	fprintf(stderr, __VA_ARGS__); \
	if (errno) \
		perror(NULL); \
	exit(1); \
})


void *xmalloc(size_t size) {
	void *ptr;
	if (ptr = malloc(size))
		return ptr;
	else
		xerror("malloc: ");
}

int main(int argc, char **argv) {
	int i;
	int ifd;
	struct stat st;
	char buf[BUFSIZE];
	struct inotify_event *pevent = (struct inotify_event *)&buf;
	struct emap *pemap;
	int tmp_events_to_watch = events_to_watch & ~IN_OPEN;
	char *fchksums;
	int fchksums_arg = 1;
	int instant_poweroff = 0;
	FILE *stream;
	struct flist dummy_head;
	struct flist **phead = &(dummy_head.next);
	struct flist **pcur;
	struct flist *cur;
	progname = strrchr(argv[0], '/');
	progname = progname ? progname + 1 : argv[0];
	memset(&dummy_head, 0, sizeof(dummy_head));
	
	if (argc > 1 && !strcmp(argv[1], "-i")) {
		fchksums_arg += 1;
		instant_poweroff = 1;
	}
	if (argc != fchksums_arg + 1 || argv[fchksums_arg][0] == '-') {
		usage();
		exit(1);
	}
	fchksums = argv[fchksums_arg];
	
	//initialize list of files to watch
	stream = fopen(fchksums, "r");
	if (stream == NULL)
		xerror("file %s: fopen: ", fchksums);
	
	for (pcur = phead; fgets(buf, BUFSIZE, stream); pcur = &((*pcur)->next)) {
		//format is: <checksum> <space> <filename>
		char *fname = strchr(buf, ' ');
		int len;
		if (fname == NULL)
			xerror("malformed checksums file\n");
		fname += 1; //skip space
		
		//allocation
		*pcur = xmalloc(sizeof(struct flist));
		len = strlen(fname);
		if (fname[len-1] == '\n') {
			fname[len-1] = '\0';
			len -= 1;
		}
		(*pcur)->name = xmalloc(len + 1);
		strcpy((*pcur)->name, fname);
	}
	fclose(stream);
	
	if (*phead == NULL)
		xerror("nothing to watch for\n");
	
	if ((ifd = inotify_init()) < 0)
		xerror("inotify_init: ");
	
	for (cur = *phead; cur != NULL; cur = cur->next) {
		if (0 != stat(cur->name, &st))
			xerror("file %s: stat: ", cur->name);
		if (!(st.st_mode & S_IFREG))
			xerror("file %s: is not a regular file\n", cur->name);
		cur->wd = inotify_add_watch(ifd, cur->name, events_to_watch);
		if (cur->wd == -1)
			xerror("inotify_add_watch: ");
	}
	
	openlog("fam", LOG_NDELAY, LOG_USER);
	
	//all files exist, all watches set up, let's process events
	while (1) {
		if (read(ifd, buf, BUFSIZE) < 0) {
			printf("read() error\n");
			continue;
		}
		//find file for which event occured
		for (cur = *phead; cur != NULL; cur = cur->next) {
			if (cur->wd == pevent->wd)
				break;
		}
		
		for (pemap = events; pemap->name; ++pemap) {
			if (pevent->mask & pemap->mask) {
				syslog(LOG_NOTICE, "%s %s", pemap->name, (cur)?(cur->name):(""));
			}
		}
		syslog(LOG_NOTICE, "%s", logmsg);
		if (!cur)
			continue;
		
		//temporary disable consequent open events
		cur->wd = inotify_add_watch(ifd, cur->name, tmp_events_to_watch);
		if (cur->wd != -1) {
			//NOTE: we no longer need inotify_event structure, so we can use it's buffer
			snprintf(buf, BUFSIZE, "cat %s | grep ' %s$' | sha1sum -c", fchksums, cur->name);
			if (system(buf) > 0) {
				syslog(LOG_EMERG, "Wrong CRC for file %s", cur->name);
				//try to go through init 0. On error make instant power off.
				if (instant_poweroff || system("sudo /sbin/shutdown -h -P now") != 0) {
					sync();
					reboot(LINUX_REBOOT_CMD_POWER_OFF);
				}
				break;
			}
		}
		//restore original events set
		cur->wd = inotify_add_watch(ifd, cur->name, events_to_watch);
	}
}
