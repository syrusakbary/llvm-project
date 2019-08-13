//===--- Unix/Watchdog.inc - Unix Watchdog Implementation -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides the generic Unix implementation of the Watchdog class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Config/config.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

namespace llvm {
  namespace sys {
    Watchdog::Watchdog(unsigned int seconds) {
#ifndef BINJI_HACK
#ifdef HAVE_UNISTD_H
      alarm(seconds);
#endif
#endif
    }

    Watchdog::~Watchdog() {
#ifndef BINJI_HACK
#ifdef HAVE_UNISTD_H
      alarm(0);
#endif
#endif
    }
  }
}
