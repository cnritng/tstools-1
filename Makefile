DIRS = lib bincat ipcat tsana tobin tscrc

# Do not print "Entering directory ..."
MAKEFLAGS += --no-print-directory

define make_dirs
	@for dir in $(DIRS); do echo; echo "---> $$dir:"; $(MAKE) -C $$dir $@; done
endef

all doc clean explain depend install uninstall:
	$(make_dirs)

tag:
	ctags -R *
