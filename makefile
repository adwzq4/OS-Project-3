CC = gcc
CFLAGS = -g -o
TARGET_1 = master
TARGET_2 = palin
OBJS_1 = master.c
OBJS_2 = palin.c

all: $(TARGET_1) $(TARGET_2)

$(TARGET_1): $(OBS_1)
	$(CC) $(CFLAGS) $(TARGET_1) $(OBJS_1)
$(TARGET_2): $(OBJS_2)
	$(CC) $(CFLAGS) $(TARGET_2) $(OBJS_2)
clean:
	/bin/rm -f *.o *.log *.out $(TARGET_1) $(TARGET_2)
