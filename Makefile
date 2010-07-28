NAME=watcher-chk
SCRIPT=watcher-log.sh

all: $(NAME)

$(NAME): $(NAME).c
	gcc $(CFLAGS) $(LDFLAGS) -o $@ $<

install:
	install -m 755 $(NAME) $(DESTDIR)/usr/sbin/$(NAME)
	install -m 755 $(SCRIPT) $(DESTDIR)/usr/sbin/$(SCRIPT)
