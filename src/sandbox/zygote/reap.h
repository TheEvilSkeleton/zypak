// Copyright 2019 Endless Mobile, Inc.
// Portions copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <set>

#include <nickle.h>

#include "base/base.h"
#include "sandbox/epoll.h"

namespace zypak::sandbox {

void HandleReap(Epoll* ep, std::set<pid_t>* children, nickle::Reader* reader);

}
