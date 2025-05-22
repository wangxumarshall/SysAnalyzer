/*
 * Copyright (c) 2023 The TraceStreamer Authors
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef PERF_SCRIPT_PARSER_H
#define PERF_SCRIPT_PARSER_H

#include <string>
#include <vector>
#include <map>
#include <set>   // Added
#include <tuple>

#include "event_parser_base.h"
#include "htrace_plugin_time_parser.h"
#include "trace_data/trace_data_cache.h"
#include "trace_streamer_filters.h"

namespace SysTuning {
namespace TraceStreamer {

class PerfScriptParser : public EventParserBase, public HtracePluginTimeParser {
public:
    PerfScriptParser(TraceDataCache* dataCache, const TraceStreamerFilters* filters);
    ~PerfScriptParser();

    void ParsePerfScript(const char* data, size_t len, bool isFinish); // Signature changed

private:
    std::string SdkBuffer_{}; // Added for segmented input
    struct ParsedEventHeader {
        std::string command;
        uint32_t pid = 0;
        uint32_t tid = 0;
        double timestamp = 0.0;
        std::string event_name;
        uint64_t raw_timestamp_ns = 0;
        uint64_t internal_timestamp_ = 0; // Added for storing converted timestamp
    };

    struct CallStackFrame {
        uint64_t ip = 0;
        std::string symbol_name;
        std::string file_name;
        DataIndex symbol_id = INVALID_DATAINDEX;
        DataIndex file_path_id = INVALID_DATAINDEX;
        uint64_t file_id = INVALID_UINT64;
        uint64_t vaddr_in_file = 0;
    };

    void ProcessCallStack(const ParsedEventHeader& event_header_data);

    std::vector<CallStackFrame> current_call_stack_data_;
    ParsedEventHeader current_event_header_data_;

    uint32_t next_call_chain_id_ = 0;
    std::map<std::vector<std::tuple<uint64_t, DataIndex, uint64_t>>, uint32_t> callchain_key_to_id_;

    uint64_t next_file_id_ = 0;
    std::map<DataIndex, uint64_t> file_path_id_to_file_id_;

    // Tracking sets
    std::set<std::pair<uint32_t, uint32_t>> known_threads_;
    std::set<DataIndex> known_event_configs_;
    std::set<std::tuple<uint64_t, DataIndex, DataIndex>> added_perf_files_entries_;

    // Cached DataIndex for common strings
    DataIndex configNameKeyIndex_ = INVALID_DATAINDEX;
    DataIndex runningStateKeyIndex_ = INVALID_DATAINDEX;
};

} // namespace TraceStreamer
} // namespace SysTuning

#endif // PERF_SCRIPT_PARSER_H
