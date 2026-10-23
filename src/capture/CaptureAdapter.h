#pragma once

#include "CaptureTypes.h"
#include <functional>
#include <string>

namespace steamdpi {

class CaptureAdapter {
public:
  using EventCallback = std::function<void(CaptureEvent)>;
  using ErrorCallback = std::function<void(std::uint32_t)>;
  virtual ~CaptureAdapter() = default;
  virtual bool start(EventCallback onEvent, ErrorCallback onError,
                     std::string& error) = 0;
  virtual void stop() = 0;
  virtual std::uint32_t lastStopError() const { return 0; }
};

} // namespace steamdpi
