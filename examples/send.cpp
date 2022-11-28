// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include <cstring>

#include "easyrdma.h"

using namespace std::chrono;
using namespace std::literals::chrono_literals;

int main(int argc, char* argv[]) {
  // Validate number of args
  if (argc < 4) {
    std::cout << "Usage:" << std::endl;
    std::cout << "  " << argv[0] << " <local address> <remote address> <remote port>" << std::endl;
    return -1;
  }

  // Parse args
  std::string localAddress = argv[1];
  std::string remoteAddress = argv[2];
  uint16_t remotePort = atoi(argv[3]);
  uint16_t localPort = 0;
  int32_t timeoutMs = 5000;
  uint32_t blockSizeBytes = 1024*1024;
  uint32_t numBuffers = 50;

  // Create session.
  easyrdma_Session session = easyrdma_InvalidSession;
  int32_t status = easyrdma_CreateConnectorSession(localAddress.c_str(), localPort, &session);
  if (status) {
    std::cout << "Error creating session: " << status;
    return -1;
  }

  // Connect to remote.  We expect the remote to already be listening.
  status = easyrdma_Connect(session, easyrdma_Direction_Send, remoteAddress.c_str(), remotePort, timeoutMs);
  if (status) {
    std::cout << "Error connecting to remote: " << status;
    return -1;
  }

  // Configure buffers.
  status = easyrdma_ConfigureBuffers(session, blockSizeBytes, numBuffers);
  if (status) {
    std::cout << "Error configuring buffers: " << status;
    easyrdma_CloseSession(session);
    return -1;
  }

  // Send data.
  uint64_t curSendSizeBytes = 0;
  std::vector<uint8_t> sendData(blockSizeBytes, 0xaa);
  auto startTime = steady_clock::now();
  while(true) {
    // Acquire a new send buffer region.
    easyrdma_InternalBufferRegion bufferRegion = {};
    status = easyrdma_AcquireSendRegion(session, timeoutMs, &bufferRegion);
    if (status) {
      std::cout << "Error acquiring send region: " << status;
      easyrdma_CloseSession(session);
      return -1;
    }

    // Fill buffer region with new data.
    if (bufferRegion.bufferSize < sendData.size()) {
      std::cout << "Send buffer too small: " << bufferRegion.bufferSize << ", " << sendData.size();
      easyrdma_CloseSession(session);
      return -1;
    }
    memcpy(bufferRegion.buffer, sendData.data(), sendData.size());
    bufferRegion.usedSize = sendData.size();

    // Queue the buffer region for send.
    status = easyrdma_QueueBufferRegion(session, &bufferRegion, nullptr /*callback*/);
    if (status) {
      std::cout << "Error queueing buffer region: " << status;
      easyrdma_CloseSession(session);
      return -1;
    }
    curSendSizeBytes += bufferRegion.usedSize;
  }
  auto endTime = steady_clock::now();

  // Calculate and print performance metrics.
  auto durationMs = duration_cast<milliseconds>(endTime - startTime).count();
  double bwGbitsPerSec = (curSendSizeBytes*8 / 1000000000.0) / (durationMs / 1000.0);
  double bwGBPerSec = (curSendSizeBytes / (1024.0*1024.0*1024.0)) / (durationMs / 1000.0);
  std::cout << "Bandwidth: " << bwGbitsPerSec << "Gbit/s; " << bwGBPerSec << "GB/s" << std::endl;

  // HACK: sleep for a bit to make sure the data makes it to the receiver.
  std::this_thread::sleep_for(500ms);

  // Close session.
  easyrdma_CloseSession(session);

  return 0;
}
