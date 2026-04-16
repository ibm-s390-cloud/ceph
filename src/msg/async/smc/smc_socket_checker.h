// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:nil -*- 
// vim: ts=8 sw=2 sts=2 expandtab

/*
 * SMC Socket Checker Platform Selector
 *
 * Provides platform-independent interface by selecting the appropriate
 * implementation based on the target platform.
 *
 * Author(s): Aliaksei Makarau <aliaksei.makarau@ibm.com>
 * 
 * Copyright IBM Corp. 2026
 */
#pragma once

#include <memory>

#include "libnetlink.h"
#include "smctools_common.h"

struct SmcSocketStats {
  int total_sockets;
  int fallback_count;

  SmcSocketStats() : total_sockets(0), fallback_count(0) {}

  void reset() {
    total_sockets = 0;
    fallback_count = 0;
  }
};

/**
 * @brief Linux implementation of SMC Socket Checker
 * 
 * This class provides Linux-specific functionality to check SMC socket
 * connections using netlink, identify fallback connections, and gather
 * statistics about SMC usage.
 */
class SmcSocketChecker {
public:
  SmcSocketChecker();
  ~SmcSocketChecker();

  SmcSocketChecker(SmcSocketChecker&& other) noexcept;
  SmcSocketChecker& operator=(SmcSocketChecker&& other) noexcept;

  int updateStatistics();
  const SmcSocketStats& getStatistics() const;
  void resetStatistics();

private:
  /**
   * @brief Process a single socket from netlink message
   * 
   * @param nlh Pointer to netlink message header
   */
  void processSocket(struct nlmsghdr *nlh);

  std::unique_ptr<NetlinkHandler> m_netlinkHandler;
  SmcSocketStats m_stats;
};
