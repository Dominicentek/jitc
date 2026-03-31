OBJDIR := obj
SOURCES := $(wildcard *.c)
OBJECTS := $(patsubst %.c,$(OBJDIR)/%.o,$(SOURCES))

CFLAGS := -g -fPIC
LDFLAGS := -g -ldl -lm

ifeq ($(OS),Windows_NT)
    LIBRARY := libjitc.dll
    STATIC_LIBRARY := libjitc.a
    TEST := jitc-tests.exe
else
    LIBRARY := libjitc.so
    STATIC_LIBRARY := libjitc.a
    TEST := jitc-tests
endif

all: $(LIBRARY) $(STATIC_LIBRARY)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(OBJDIR)
	gcc -c $< $(CFLAGS) -o $@

$(LIBRARY): $(OBJECTS)
	gcc $^ $(LDFLAGS) -shared -o $@

$(STATIC_LIBRARY): $(OBJECTS)
	ar rcs $@ $^

$(TEST): $(STATIC_LIBRARY) tester/test.c
	gcc $^ -L. -ljitc $(CFLAGS) $(LDFLAGS) -o $@

test: $(TEST)
	@./$(TEST) $(shell find tests -name "*.c")

clean:
	rm -rf $(LIBRARY) $(STATIC_LIBRARY) $(TEST) $(OBJDIR)

$(OBJDIR)/%.d: %.c
	@mkdir -p $(shell dirname $@)
	gcc $(CFLAGS) -MM -MT $(@:.d=.o) $^ -o $@

-include $(OBJECTS:.o=.d)