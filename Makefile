CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -Iinclude \
            $(shell pkg-config --cflags libevdev jsoncpp)
LIBS := $(shell pkg-config --libs libevdev jsoncpp)

TARGET   := PointerForce
SRCDIR   := src
OBJDIR   := obj

SRCS := $(wildcard $(SRCDIR)/*.cpp)
OBJS := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SRCS))

.PHONY: all clean install uninstall debug

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)
	@echo "  LD  $@"

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	@echo "  CXX $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

debug: CXXFLAGS += -g -DDEBUG -O0
debug: $(TARGET)

clean:
	rm -rf $(OBJDIR) $(TARGET)

install: all
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)
	install -m 644 config/pointerforce.json /etc/pointerforce.json
	install -m 644 pointerforce.service /etc/systemd/system/
	systemctl daemon-reload
	@echo "Installed. Enable with: systemctl enable --now pointerforce"

uninstall:
	systemctl stop pointerforce 2>/dev/null || true
	systemctl disable pointerforce 2>/dev/null || true
	rm -f /usr/local/bin/$(TARGET)
	rm -f /etc/pointerforce.json
	rm -f /etc/systemd/system/pointerforce.service
	systemctl daemon-reload
