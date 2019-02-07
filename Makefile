# Best Makefile tutorial: https://stackoverflow.com/a/30602701/9555588

# CC = clang

EXE = morse
SRC_DIR = src
OBJ_DIR = obj

SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ = $(SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

FRAMEWORKS = -framework CoreFoundation -framework ApplicationServices

LDFLAGS += -L/usr/local/opt/curl/lib
LDFLAGS += -L/usr/local/Cellar/libsodium/1.0.17/lib
LDFLAGS += -L/usr/local/Cellar/cmark-gfm/0.28.3.gfm.16/lib

LDLIBS += -lpthread
LDLIBS += -lcurl
LDLIBS += -lcmark-gfm
LDLIBS += -lsodium

CPPFLAGS += -Iinclude  # default "include" directory
CPPFLAGS += -I/usr/local/opt/curl/include

.PHONY: all clean

all: $(EXE)

$(EXE): $(OBJ)
	$(CC) $(LDFLAGS) $^ $(CMARK) $(SODIUM) $(FRAMEWORKS) $(LDLIBS) -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJ)
