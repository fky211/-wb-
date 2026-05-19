# Charger Monitor Web Server Makefile
# 编译环境: Linux (需要sqlite3开发库)

CC = gcc
CFLAGS = -Wall -Wextra -O2 -I./src
LDFLAGS = -lsqlite3 -lpthread

# 源文件
SRCDIR = src
SRCS = $(SRCDIR)/main.c \
       $(SRCDIR)/http_server.c \
       $(SRCDIR)/sqlite_db.c \
       $(SRCDIR)/json_utils.c \
       $(SRCDIR)/logger.c

# 目标文件
OBJS = $(SRCS:.c=.o)
TARGET = charger_server

# 默认目标
all: $(TARGET)

# 链接
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Build success: $(TARGET)"

# 编译源文件
$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# 清理
clean:
	rm -f $(OBJS) $(TARGET) charger.db
	@echo "Cleaned."

# 运行
run: $(TARGET)
	./$(TARGET)

# 调试版本
debug: CFLAGS += -g -DDEBUG
debug: clean $(TARGET)

# 安装依赖 (Ubuntu/Debian)
install-deps:
	sudo apt-get update
	sudo apt-get install -y libsqlite3-dev gcc make

# 格式化代码
format:
	@which indent > /dev/null 2>&1 && indent -kr -i4 -ts4 $(SRCS) || echo "indent not found"

.PHONY: all clean run debug install-deps format
