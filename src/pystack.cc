// This file is part of Pystack.
//
// Pystack is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Pystack is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Pystack.  If not, see <http://www.gnu.org/licenses/>.

#include <getopt.h>

#include <chrono>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

#include "./config.h"
#include "./exc.h"
#include "./ptrace.h"
#include "./pyframe.h"

using namespace pystack;

namespace {
const char usage_str[] = "Usage: pystack [-h|--help] PID\n";

void RunOnce(pid_t pid, unsigned long addr) {
  std::vector<Frame> stack = GetStack(pid, addr);
  for (auto it = stack.rbegin(); it != stack.rend(); it++) {
    std::cout << *it << "\n";
  }
  std::cout << std::flush;
}

typedef std::vector<Frame> frames_t;

struct FrameHash {
  size_t operator()(const frames_t &frames) const {
    size_t hash = 0;
    for (size_t i = 0; i < frames.size(); i++) {
      hash ^= std::hash<size_t>()(i);
      hash ^= std::hash<std::string>()(frames[i].file());
    }
    return hash;
  }
};
}  // namespace

int main(int argc, char **argv) {
  double seconds = 0;
  double sample_rate = 0.01;
  for (;;) {
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"rate", required_argument, 0, 'r'},
        {"seconds", required_argument, 0, 's'},
        {"version", no_argument, 0, 'v'},
        {0, 0, 0, 0}};
    int option_index = 0;
    int c = getopt_long(argc, argv, "hjr:s:v", long_options, &option_index);
    if (c == -1) {
      break;
    }
    switch (c) {
      case 0:
        if (long_options[option_index].flag != 0) {
          // if the option set a flag, do nothing
          break;
        }
        break;
      case 'h':
        std::cout << usage_str;
        return 0;
        break;
      case 'r':
        sample_rate = std::stod(optarg);
        break;
      case 's':
        seconds = std::stod(optarg);
        break;
      case 'v':
        std::cout << PACKAGE_STRING << "\n";
        return 0;
        break;
      case '?':
        // getopt_long should already have printed an error message
        break;
      default:
        abort();
    }
  }
  if (optind != argc - 1) {
    std::cerr << usage_str;
    return 1;
  }
  long pid = std::strtol(argv[argc - 1], nullptr, 10);
  if (pid > std::numeric_limits<pid_t>::max() ||
      pid < std::numeric_limits<pid_t>::min()) {
    std::cerr << "PID " << pid << " is out of valid PID range.\n";
    return 1;
  }
  try {
    PtraceAttach(pid);
    const unsigned long addr = ThreadStateAddr(pid);
    if (seconds) {
      const std::chrono::microseconds interval{
          static_cast<long>(sample_rate * 1000000)};
      std::unordered_map<frames_t, size_t, FrameHash> buckets;
      size_t empty = 0;
      auto end =
          std::chrono::system_clock::now() +
          std::chrono::microseconds(static_cast<long>(seconds * 1000000));
      for (;;) {
        try {
          frames_t frames = GetStack(pid, addr);
          auto it = buckets.find(frames);
          if (it == buckets.end()) {
            buckets.insert(it, {frames, 1});
          } else {
            it->second++;
          }
        } catch (const NonFatalException &exc) {
          empty++;
        }
        auto now = std::chrono::system_clock::now();
        if (now + interval >= end) {
          break;
        }
        PtraceDetach(pid);
        std::this_thread::sleep_for(interval);
        PtraceAttach(pid);
      }
      if (empty) {
        std::cout << "(null) " << empty << "\n";
      }
      // process the frames
      for (const auto &kv : buckets) {
        if (kv.first.empty()) {
          std::cerr << "uh oh\n";
          return 1;
        }
        auto last = kv.first.rend();
        last--;
        for (auto it = kv.first.rbegin(); it != last; ++it) {
          std::cout << *it << ";";
        }
        std::cout << *last << " " << kv.second << "\n";
      }
    } else {
      RunOnce(pid, addr);
    }
  } catch (const FatalException &exc) {
    std::cerr << exc.what() << std::endl;
    return 1;
  } catch (const NonFatalException &exc) {
    std::cerr << exc.what() << std::endl;
    return 0;
  } catch (const std::exception &exc) {
    std::cerr << exc.what() << std::endl;
    return 1;
  }
  return 0;
}
