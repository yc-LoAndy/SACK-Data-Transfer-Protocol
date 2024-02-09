CC = gcc
CXX = g++
LINK = -lrt -lssl -lcrypto -lz -lm
CXXFLAG = -std=c++20 -g
CFLAG = -std=c99

SENDER = sender.c
RECEIVER = receiver.c
AGENT = agent.cpp
HEADER = def.h
CRC32 = crc32.cpp
SHA256 = sha256.c
SND = sender
RCV = receiver
AGT = agent
CRC = crc
SHA = sha

all: sender receiver agent
  
sender: $(SENDER) $(HEADER)
	$(CC) $(SENDER) -o $(SND) $(LINK) $(CFLAG)
receiver: $(RECEIVER) $(HEADER)
	$(CC) $(RECEIVER) -o $(RCV) $(LINK) $(CFLAG)
agent: $(AGENT) $(HEADER)
	$(CXX) $(AGENT) -o $(AGT) $(LINK) $(CXXFLAG)
# crc32: $(CRC32)
# 	$(CXX) $(CRC32) -o $(CRC) $(LINK) $(CXXFLAG)
# sha256: $(SHA256)
# 	$(CC) $(SHA256) -o $(SHA) $(LINK) $(CFLAG)

.PHONY: clean

clean:
	rm $(SND) $(RCV) $(AGT)
