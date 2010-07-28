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

void usage(void) {
	printf("\n\
notify - report events on watched files to syslog and compute checksums. \n\
\n\
notify -c <chksums file>\n\
");
}

#define BUFSIZE (sizeof(struct inotify_event) + PATH_MAX)
#define MSGSIZE (256 + PATH_MAX)

int events_to_watch = IN_DELETE_SELF | IN_MOVE_SELF | IN_OPEN | IN_MODIFY;
struct emap {
	uint32_t mask;
	char *name;
};
struct emap events[] = {
	{IN_DELETE_SELF, "DELETE"},
	{IN_MOVE_SELF, "MOVE"},
	{IN_OPEN, "OPEN"},
	{IN_MODIFY, "MODIFY"},
	{0, NULL},
};
struct flist {
	char *name;
	int wd;
	struct flist *next;
};

void *xmalloc(size_t size) {
	void *ptr;
	if (ptr = malloc(size)) {
		return ptr;
	} else {
		printf("memory allocation failed\n");
		exit(1);
	}
}

int main(int argc, char **argv) {
	int i;
	int ifd;
	struct stat st;
	char buf[BUFSIZE];
	struct inotify_event *pevent = (struct inotify_event *)&buf;
	char logmsg[MSGSIZE];
	char *cmd = logmsg;
	char csv_events[MSGSIZE];
	struct emap *pemap;
	int tmp_events_to_watch = events_to_watch & ~IN_OPEN;
	char *fchksums = argv[1];
	FILE *stream;
	struct flist dummy_head;
	struct flist **phead = &(dummy_head.next);
	struct flist **pcur;
	struct flist *cur;
	memset(&dummy_head, 0, sizeof(dummy_head));
	
	if (argc != 2 || argv[1][0] == '-') {
		usage();
		exit(1);
	}
	
	//initialize list of files to watch
	stream = fopen(fchksums, "r");
	if (stream == NULL) {
		perror("inotify: fopen");
		exit(1);
	}
	
	for (pcur = phead; fgets(buf, MSGSIZE, stream); pcur = &((*pcur)->next)) {
		//format is: <checksum> <space> <filename>
		char *fname = strchr(buf, ' ');
		int len;
		if (fname == NULL) {
			printf("malformed checksums file\n");
			exit(1);
		}
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
	
	if (*phead == NULL) {
		printf("nothing to watch for\n");
		exit(1);
	}
	
	if ((ifd = inotify_init()) < 0) {
		perror("inotify: inotify_init");
		exit(1);
	}
	
	for (cur = *phead; cur != NULL; cur = cur->next) {
		printf("file: %s\n", cur->name);
		if (0 != stat(cur->name, &st)) {
			perror("inotify: stat");
			exit(1);
		}
		if (!(st.st_mode & S_IFREG)) {
			printf("%s is not a regular file\n");
			exit(1);
		}
		cur->wd = inotify_add_watch(ifd, cur->name, events_to_watch);
		if (cur->wd == -1) {
			perror("inotify: inotify_add_watch");
			exit(1);
		}
	}
	
	openlog("inotify", LOG_NDELAY, LOG_USER);
	
	//all files exist, all watches set up, let's process events
	while (1) {
		csv_events[0] = '\0';
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
				strncat(csv_events, pemap->name, MSGSIZE);
				csv_events[MSGSIZE - 1] = '\0';
			}
		}
		snprintf(logmsg, MSGSIZE,
			"Got event %s for %s",
			csv_events,
			(cur)?(cur->name):("")
		);
		syslog(LOG_NOTICE, "%s", logmsg);
		if (!cur)
			continue;
		
		//temporary disable consequent open events
		cur->wd = inotify_add_watch(ifd, cur->name, tmp_events_to_watch);
		if (cur->wd != -1) {
			//read event disabled, let's compute checksum
			snprintf(cmd, MSGSIZE, "sha1sum -c%s %s", fchksums, cur->name);
//			snprintf(cmd, MSGSIZE, "md5sum -c %s", fchksums);
			if (system(cmd) > 0) {
				//system() call was successful but gostsum returned error
				snprintf(logmsg, MSGSIZE,
					"Wrong CRC for file %s",
					cur->name
				);
				syslog(LOG_EMERG, "%s", logmsg);
				sync();
				reboot(LINUX_REBOOT_CMD_HALT);
				break;
			}
		}
		//restore original events set
		cur->wd = inotify_add_watch(ifd, cur->name, events_to_watch);
	}
}
