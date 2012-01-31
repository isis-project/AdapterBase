OBJDIR := $(BUILD_TYPE)-$(PLATFORM)

# ------------------------------------------------------------------


TARGET_SO := $(OBJDIR)/AdapterBase.a

TARGET_SO_OBJS := \
	$(OBJDIR)/AdapterBase.o 


# ------------------------------------------------------------------

FLAGS_COMMON := -fno-exceptions -fno-rtti -fvisibility=hidden -fPIC -DXP_UNIX -DXP_WEBOS

ifeq ("$(BUILD_TYPE)", "debug")
FLAGS_OPT := -O0 -g $(FLAGS_COMMON) -DDEBUG
else
FLAGS_OPT := -O2 -g $(FLAGS_COMMON) -DNDEBUG
endif

INCLUDES := $(INCLUDES) \
	-I. \
	`pkg-config --cflags glib-2.0`

LIBS := $(LIBS) \
	`pkg-config --lflags glib-2.0`


LOCAL_CFLAGS := $(CFLAGS) -Wall -Werror $(FLAGS_OPT) 

LOCAL_LFLAGS := $(LDFLAGS) $(FLAGS_OPT) $(LIBS) -Wl

# ------------------------------------------------------------------

all: stage $(TARGET_SO)

stage:
	@mkdir -p $(OBJDIR)

$(TARGET_SO): $(TARGET_SO_OBJS) 
	ar cr $(OBJDIR)/AdapterBase.a   $(OBJDIR)/AdapterBase.o 

vpath %.cpp

$(OBJDIR)/%.o: %.cpp *.h
	$(CXX) $(INCLUDES) $(LOCAL_CFLAGS) -c $< -o $@

clean::
	rm -rf $(OBJDIR)

