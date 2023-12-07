CC=gcc
CXX=g++
OBJ = command.o debug_msg.o device.o image.o instance.o main.o memory.o shader.o stbi.o watch_linux.o window.o
CFLAGS=-Wall -Wextra -Werror -fsanitize=address,leak,undefined -fno-omit-frame-pointer
LIBS=-lglfw -lvulkan -llogger -lshaderc_shared -lm -lshaderc_shared -lvma 

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)
a.out: $(OBJ)
	$(CC) -o $@ $^ $(LIBS) $(CFLAGS)
libvma.so: vma.cpp
	$(CXX) -lvulkan -O0 -ggdb -shared -fPIC -o libvma.so
.PHONY: clean
clean:
	rm -f *.o
