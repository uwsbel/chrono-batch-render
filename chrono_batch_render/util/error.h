// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifdef CHRONO_BATCH_RENDER_FMT
#include <fmt/format.h>
#endif

#include "chrono_batch_render/export.h"

namespace chrono::batch_render {

class CHRONO_BATCH_RENDER_API Error : public std::runtime_error {
  public:
    explicit Error(const std::string& message);

#ifdef CHRONO_BATCH_RENDER_FMT
    template <class... Args>
        requires(sizeof...(Args) > 0)
    explicit Error(fmt::format_string<Args...> format, Args&&... args)
        : Error(fmt::format(format, std::forward<Args>(args)...)) {}
#endif

    ~Error() override;
};

class CHRONO_BATCH_RENDER_API UnsupportedError : public Error {
  public:
    using Error::Error;
    ~UnsupportedError() override;
};

class CHRONO_BATCH_RENDER_API DeviceLostError : public Error {
  public:
    using Error::Error;
    ~DeviceLostError() override;
};

class CHRONO_BATCH_RENDER_API ApiError : public Error {
  public:
    ApiError(std::string call, std::string errorName, long code, std::string_view description = {});
    ~ApiError() override;

    const std::string& Call() const {
        return m_call;
    }

    const std::string& ErrorName() const {
        return m_errorName;
    }

    long Code() const {
        return m_code;
    }

  private:
    std::string m_call;
    std::string m_errorName;
    long m_code = 0;
};

}
