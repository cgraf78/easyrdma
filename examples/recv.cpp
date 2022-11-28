// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <exception>

#include "easyrdma.h"

using namespace std::chrono;
using namespace std::literals::chrono_literals;

struct easyrdma_Error : std::exception {
  easyrdma_Error(int32_t c) : code(c) {}

  int32_t code;
};

int main(int argc, char* argv[]) {
  easyrdma_Session listenSession = easyrdma_InvalidSession;
  easyrdma_Session connectedSession = easyrdma_InvalidSession;

  try {
    // Validate number of args
    if (argc < 3) {
      std::cout << "Usage:" << std::endl;
      std::cout << "  " << argv[0] << " <local address> <local port>" << std::endl;
      return -1;
    }

    // Parse args
    std::string localAddress = argv[1];
    uint16_t localPort = atoi(argv[2]);
    int32_t timeoutMs = -1;
    uint32_t blockSizeBytes = 2*1024*1024;
    uint32_t numBuffers = 10;

    // Create the listener session.
    int32_t status = easyrdma_CreateListenerSession(localAddress.c_str(), localPort, &listenSession);
    if (status) {
      std::cout << "Error creating session: " << status;
      throw easyrdma_Error(status);
    }

    while (true) {
      // Wait for connection.
      std::cout << "Waiting for connection..." << std::flush;
      status = easyrdma_Accept(listenSession, easyrdma_Direction_Receive, timeoutMs, &connectedSession);
      if (status) {
        std::cout << "Error accepting connection: " << status;
        throw easyrdma_Error(status);
      }
      std::cout << " accepted" << std::endl;

      // Configure buffers.
      status = easyrdma_ConfigureBuffers(connectedSession, blockSizeBytes, numBuffers);
      if (status == easyrdma_Error_Disconnected) {
        std::cout << "Disconnected" << std::endl;
      } else if (status) {
        std::cout << "Error configuring buffers: " << status;
        throw easyrdma_Error(status);
      }

      // Receive data.
      uint64_t totalReceivedBytes = 0;
      uint64_t numReceivedBytes = 0;
      auto lastTime = steady_clock::now();
      while (!status) {
        // Acquire a new buffer region.
        easyrdma_InternalBufferRegion bufferRegion = {};
        status = easyrdma_AcquireReceivedRegion(connectedSession, timeoutMs, &bufferRegion);
        if (status == easyrdma_Error_Disconnected) {
          std::cout << "Disconnected" << std::endl;
          break;
        } else if (status) {
          std::cout << "Error acquiring received region: " << status;
          throw easyrdma_Error(status);
        }

        // Count amount of data received.
        numReceivedBytes += bufferRegion.usedSize;
        totalReceivedBytes += bufferRegion.usedSize;

        // Done with the receive buffer.  Release it.
        status = easyrdma_ReleaseReceivedBufferRegion(connectedSession, &bufferRegion);
        if (status == easyrdma_Error_Disconnected) {
          std::cout << "Disconnected" << std::endl;
          break;
        } else if (status) {
          std::cout << "Error releasing received region: " << status;
          throw easyrdma_Error(status);
        }

        // Calculate and print performance metrics.
        auto durationMs = duration_cast<milliseconds>(steady_clock::now() - lastTime).count();
        if (durationMs >= 1000) {
          double bwGbitsPerSec = (numReceivedBytes*8 / 1000000000.0) / (durationMs / 1000.0);
          double bwGBPerSec = (numReceivedBytes / (1024.0*1024.0*1024.0)) / (durationMs / 1000.0);
          std::cout << "Bandwidth: " << bwGbitsPerSec << "Gbit/s; " << bwGBPerSec << "GB/s" << std::endl;

          lastTime = steady_clock::now();
          numReceivedBytes = 0;
        }
      }

      // Print total received bytes for the connected session.
      std::cout << "Received " << totalReceivedBytes << " bytes" << std::endl;

      // Clean up connected session.
      easyrdma_CloseSession(connectedSession);
      connectedSession = easyrdma_InvalidSession;
    }

    // Clean up listen session.
    easyrdma_CloseSession(listenSession);
    listenSession = easyrdma_InvalidSession;

    return 0;
  }
  catch (std::exception&) {
    if (listenSession != easyrdma_InvalidSession)
      easyrdma_CloseSession(listenSession);
    if (connectedSession != easyrdma_InvalidSession)
      easyrdma_CloseSession(connectedSession);
    return -1;
  }
}
