// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "chrono_batch_render/export.h"

namespace chrono::batch_render::profiler {

class CHRONO_BATCH_RENDER_API ZoneId {
  public:
    static ZoneId Named(std::string_view name);

    std::string_view Name() const;

    uint32_t Index() const {
        return m_index;
    }

    friend bool operator==(ZoneId a, ZoneId b) {
        return a.m_index == b.m_index;
    }

    friend bool operator!=(ZoneId a, ZoneId b) {
        return a.m_index != b.m_index;
    }

  private:
    explicit ZoneId(uint32_t index) : m_index(index) {}

    uint32_t m_index;
};

struct Track {
    std::string name;
    bool gpu = false;
};

struct Zone {
    ZoneId id;
    uint32_t track = 0;
    uint32_t depth = 0;
    uint64_t startNs = 0;
    uint64_t endNs = 0;
};

struct Frame {
    uint64_t index = 0;
    uint64_t startNs = 0;
    uint64_t endNs = 0;
    std::vector<Zone> zones;
};

CHRONO_BATCH_RENDER_API void SetEnabled(bool enabled);
CHRONO_BATCH_RENDER_API bool Enabled();

CHRONO_BATCH_RENDER_API uint64_t Now();

CHRONO_BATCH_RENDER_API void BeginFrame();

CHRONO_BATCH_RENDER_API uint64_t CurrentFrame();

CHRONO_BATCH_RENDER_API std::vector<Frame> Frames();

CHRONO_BATCH_RENDER_API void SetHistory(size_t frames);

CHRONO_BATCH_RENDER_API void Clear();

CHRONO_BATCH_RENDER_API void SetThreadName(std::string_view name);

CHRONO_BATCH_RENDER_API std::vector<Track> Tracks();

CHRONO_BATCH_RENDER_API uint32_t AddGpuTrack(std::string_view name);

CHRONO_BATCH_RENDER_API void AddZone(uint64_t frame, const Zone& zone);

struct ReportNode {
    std::string name;
    double ms = 0;
    double calls = 0;
    double percent = 0;
    double minMs = 0;
    double maxMs = 0;
    std::vector<ReportNode> children;
};

struct ReportTrack {
    std::string name;
    bool gpu = false;
    double ms = 0;
    std::vector<ReportNode> zones;
};

struct Report {
    size_t frames = 0;
    double frameMs = 0;
    std::vector<ReportTrack> tracks;
};

CHRONO_BATCH_RENDER_API Report Summarize(const std::vector<Frame>& frames);

struct ReportFilter {
    uint32_t depth = 8;
    double minMs = 0;
    bool byTime = false;
};

CHRONO_BATCH_RENDER_API std::string ReportText(const Report& report,
                                               const ReportFilter& filter = {});

CHRONO_BATCH_RENDER_API std::string ReportJson(const Report& report);

CHRONO_BATCH_RENDER_API std::string TraceJson(const std::vector<Frame>& frames);

class CHRONO_BATCH_RENDER_API ScopedZone {
  public:
    explicit ScopedZone(ZoneId zone);
    ~ScopedZone();
    ScopedZone(const ScopedZone&) = delete;
    ScopedZone& operator=(const ScopedZone&) = delete;

  private:
    ZoneId m_zone;
    uint64_t m_startNs = 0;
    bool m_recording = false;
    uint8_t m_ranges = 0;
};

}

#define CHRONO_BATCH_RENDER_PROFILER_CONCAT_(a, b) a##b
#define CHRONO_BATCH_RENDER_PROFILER_CONCAT(a, b) CHRONO_BATCH_RENDER_PROFILER_CONCAT_(a, b)

#define CHRONO_BATCH_RENDER_ZONE(name)                                                             \
    static const ::chrono::batch_render::profiler::ZoneId CHRONO_BATCH_RENDER_PROFILER_CONCAT(     \
        chronoBatchRenderZoneId, __LINE__) =                                                       \
        ::chrono::batch_render::profiler::ZoneId::Named(name);                                     \
    const ::chrono::batch_render::profiler::ScopedZone CHRONO_BATCH_RENDER_PROFILER_CONCAT(        \
        chronoBatchRenderZone,                                                                     \
        __LINE__)(CHRONO_BATCH_RENDER_PROFILER_CONCAT(chronoBatchRenderZoneId, __LINE__))
