CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -Iinclude -pthread \
            $(shell pkg-config --cflags libevdev jsoncpp)
LIBS     := $(shell pkg-config --libs libevdev jsoncpp) -pthread

DAEMON_TARGET := run_pointerforce
CTL_TARGET    := pfctl

SRCDIR := src
OBJDIR := obj

DAEMON_SRCS := \
    $(SRCDIR)/main.cpp         \
    $(SRCDIR)/config.cpp       \
    $(SRCDIR)/device.cpp       \
    $(SRCDIR)/devicemanager.cpp \
    $(SRCDIR)/eventqueue.cpp   \
    $(SRCDIR)/multiplexer.cpp  \
    $(SRCDIR)/mapper.cpp       \
    $(SRCDIR)/executor.cpp     \
    $(SRCDIR)/daemon.cpp       \
    $(SRCDIR)/control.cpp

CTL_SRCS := $(SRCDIR)/pfctl.cpp

DAEMON_OBJS := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(DAEMON_SRCS))
CTL_OBJS    := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(CTL_SRCS))

.PHONY: all clean install uninstall debug asan

all: $(DAEMON_TARGET) $(CTL_TARGET)

$(DAEMON_TARGET): $(DAEMON_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)
	@echo "  LD  $@"

$(CTL_TARGET): $(CTL_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)
	@echo "  LD  $@"

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	@echo "  CXX $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

debug: CXXFLAGS += -g -DDEBUG -O0
debug: $(DAEMON_TARGET) $(CTL_TARGET)

clean:
	rm -rf $(OBJDIR) $(DAEMON_TARGET) $(CTL_TARGET)

install: all
	install -m 755 $(DAEMON_TARGET) /usr/local/bin/$(DAEMON_TARGET)
	install -m 755 $(CTL_TARGET)    /usr/local/bin/$(CTL_TARGET)
	install -m 644 config/pointerforce.json /etc/pointerforce.json
	install -m 644 pointerforce.service /etc/systemd/system/
	install -d /var/log
	touch /var/log/pointerforce.log
	systemctl daemon-reload
	@echo "Installed. Enable with: systemctl enable --now pointerforce"

uninstall:
	systemctl stop    pointerforce 2>/dev/null || true
	systemctl disable pointerforce 2>/dev/null || true
	rm -f /usr/local/bin/$(DAEMON_TARGET)
	rm -f /usr/local/bin/$(CTL_TARGET)
	rm -f /etc/pointerforce.json
	rm -f /etc/systemd/system/pointerforce.service
	systemctl daemon-reload

asan: CXXFLAGS += -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer
asan: LIBS     += -fsanitize=address,undefined
asan: $(DAEMON_TARGET) $(CTL_TARGET)
