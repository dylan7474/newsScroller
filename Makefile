# Makefile for Linux compilation
#
# To compile, run: make -f Makefile.linux
# To run, execute: ./news_ticker
#
# Ensure you have the required libraries installed:
# sudo apt-get install build-essential libsdl2-dev libsdl2-ttf-dev libcurl4-openssl-dev

CC = gcc
TARGET = news_ticker
# Add cJSON.c to the source files
SRCS = main.c cJSON.c
# Add -g for debugging symbols. Add SDL_ttf flags.
CFLAGS = -Wall -O2 `sdl2-config --cflags` -I.
LDFLAGS = `sdl2-config --libs` -lSDL2_ttf -lcurl -lm

all: $(TARGET)

$(TARGET): $(SRCS) cJSON.h
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)
