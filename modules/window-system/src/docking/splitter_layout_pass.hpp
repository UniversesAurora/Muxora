// SPDX-License-Identifier: MIT
#pragma once

namespace window_system {

// Marks a synchronous geometry pass of a splitter that holds group strips
// (resize, setSizes, handle drag, child changes). Such a splitter iterates
// its item list while positioning children, and a host can mutate the layout
// from a content resize in the middle of it. A surface rebuild during a pass
// must not reparent strips out of the old splitters. GUI thread only.
class SplitterLayoutPass final {
public:
    SplitterLayoutPass() noexcept { ++depth(); }
    ~SplitterLayoutPass() { --depth(); }
    SplitterLayoutPass(const SplitterLayoutPass&) = delete;
    SplitterLayoutPass& operator=(const SplitterLayoutPass&) = delete;
    [[nodiscard]] static bool active() noexcept { return depth() > 0; }

private:
    static int& depth() noexcept
    {
        static int value = 0;
        return value;
    }
};

} // namespace window_system
