// Copyright 2026 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

// Listed only in hornetlib_checked_tests: fails the build if the target ever loses
// HORNETLIB_CHECK_MAGNITUDES, which would turn every checked-mode contract test into a
// silently-passing no-op.

#include "hornetlib/crypto/element.h"

static_assert(hornet::crypto::ecdsa::kCheckMagnitudes,
              "hornetlib_checked_tests must be compiled with HORNETLIB_CHECK_MAGNITUDES=1");
