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

#include "perf_script_parser.h"

#include <sstream>
#include <regex>
#include <iomanip>

#include "log.h"
#include "string_to_numerical.h"
#include "clock_filter.h" // Required for TSClockType

namespace SysTuning {
namespace TraceStreamer {

PerfScriptParser::PerfScriptParser(TraceDataCache* dataCache, const TraceStreamerFilters* filters)
    : EventParserBase(dataCache, filters),
      HtracePluginTimeParser(dataCache, filters),
      current_event_header_data_({"", 0, 0, 0.0, "", 0, 0}), // Initialize with pid = 0 and internal_timestamp_ = 0
      next_call_chain_id_(0),
      next_file_id_(0)
{
    known_threads_.clear();
    known_event_configs_.clear();
    added_perf_files_entries_.clear();
    if (traceDataCache_) { // Ensure dataCache is valid before accessing
        configNameKeyIndex_ = traceDataCache_->GetDataDict().GetStringIndex("config_name");
        runningStateKeyIndex_ = traceDataCache_->GetDataDict().GetStringIndex("Running");
    } else {
        TS_LOGE("TraceDataCache is null in PerfScriptParser constructor");
        // Handle error or ensure dataCache is always non-null
    }
}

PerfScriptParser::~PerfScriptParser()
{
    TS_LOGI("PerfScriptParser destroyed. Processed %zu unique callchains, %zu unique files.",
            callchain_key_to_id_.size(), file_path_id_to_file_id_.size());
}

// Modified for segmented input
void PerfScriptParser::ParsePerfScript(const char* data, size_t len, bool isFinish)
{
    if (data == nullptr && len > 0) {
        TS_LOGE("Invalid data pointer passed to PerfScriptParser");
        return;
    }
    if (len > 0) {
        SdkBuffer_.append(data, len);
    }

    if (!isFinish) {
        TS_LOGD("PerfScriptParser accumulated %zu bytes, waiting for more data.", SdkBuffer_.length());
        return;
    }

    TS_LOGI("Starting to parse perf script output (%zu bytes accumulated)", SdkBuffer_.length());
    if (SdkBuffer_.empty()) {
        TS_LOGI("No data to parse in PerfScriptParser.");
        return; // Nothing to do
    }
    std::istringstream stream(SdkBuffer_);
    std::string line;
    int lineCount = 0;

    std::regex eventHeaderRegex(R"((.+?)\s+(\d+)(?:/(\d+))?\s+([\d.]+):\s*(?:.*?)\s*([a-zA-Z0-9_-]+):\s*$)");
    std::regex callStackRegex(R"(^\s*([0-9a-fA-Fx]+)\s+(.+?)(?:\s+\((.+?)\))?\s*$)");

    current_event_header_data_ = {}; // Ensure it's reset

    while (std::getline(stream, line)) {
        lineCount++;
        if (line.empty()) {
            continue;
        }

        std::smatch match;
        if (std::regex_match(line, match, eventHeaderRegex)) {
            if (current_event_header_data_.pid != 0) {
                ProcessCallStack(current_event_header_data_);
            }
            current_call_stack_data_.clear();

            current_event_header_data_.command = match[1].str();
            current_event_header_data_.pid = static_cast<uint32_t>(std::stoul(match[2].str()));
            current_event_header_data_.tid = current_event_header_data_.pid;
            if (match[3].matched) {
                current_event_header_data_.tid = static_cast<uint32_t>(std::stoul(match[3].str()));
            }
            current_event_header_data_.timestamp = std::stod(match[4].str());
            current_event_header_data_.event_name = match[5].str();
            current_event_header_data_.raw_timestamp_ns = static_cast<uint64_t>(current_event_header_data_.timestamp * 1e9);

            // Time Handling
            // Assuming TS_MONOTONIC for perf script timestamps. This might need to be configurable.
            current_event_header_data_.internal_timestamp_ = streamFilters_->clockFilter_->ToPrimaryTraceTime(
                TS_MONOTONIC, current_event_header_data_.raw_timestamp_ns);
            UpdatePluginTimeRange(TS_MONOTONIC, current_event_header_data_.raw_timestamp_ns,
                                  current_event_header_data_.internal_timestamp_);

            // Threads (PerfThreadData)
            if (known_threads_.find({current_event_header_data_.pid, current_event_header_data_.tid}) == known_threads_.end()) {
                DataIndex command_idx = traceDataCache_->GetDataDict().GetStringIndex(current_event_header_data_.command.c_str());
                traceDataCache_->GetPerfThreadData()->AppendNewPerfThread(current_event_header_data_.pid, current_event_header_data_.tid, command_idx);
                known_threads_.insert({current_event_header_data_.pid, current_event_header_data_.tid});
                TS_LOGD("Added Thread: PID: %u, TID: %u, Comm: %s (idx: %lu)", current_event_header_data_.pid, current_event_header_data_.tid, current_event_header_data_.command.c_str(), command_idx);
            }

            // Report Data (PerfReportData for event names)
            DataIndex event_name_idx = traceDataCache_->GetDataDict().GetStringIndex(current_event_header_data_.event_name.c_str());
            if (known_event_configs_.find(event_name_idx) == known_event_configs_.end()) {
                // configNameKeyIndex_ is cached in constructor
                traceDataCache_->GetPerfReportData()->AppendNewPerfReport(configNameKeyIndex_, event_name_idx);
                known_event_configs_.insert(event_name_idx);
                TS_LOGD("Added Report Config: Event Name: %s (idx: %lu)", current_event_header_data_.event_name.c_str(), event_name_idx);
            }
            
            TS_LOGD("Event: Comm: %s, PID: %u, TID: %u, TS: %f (Raw: %" PRIu64 "ns, Internal: %" PRIu64 "ns), Event: %s (idx: %lu)",
                     current_event_header_data_.command.c_str(), current_event_header_data_.pid, current_event_header_data_.tid,
                     current_event_header_data_.timestamp, current_event_header_data_.raw_timestamp_ns,
                     current_event_header_data_.internal_timestamp_, current_event_header_data_.event_name.c_str(), event_name_idx);

        } else if (std::regex_match(line, match, callStackRegex)) {
            if (current_event_header_data_.pid == 0) {
                 TS_LOGW("Callstack line found without a preceding event header: %s", line.c_str());
                 continue;
            }
            CallStackFrame frame;
            try {
                frame.ip = std::stoull(match[1].str(), nullptr, 16);
            } catch (const std::out_of_range&) { frame.ip = 0; }
              catch (const std::invalid_argument&) { frame.ip = 0;}

            frame.symbol_name = match[2].str();
            if (match[3].matched) {
                frame.file_name = match[3].str();
                if (!frame.file_name.empty() && frame.file_name.front() == '[' && frame.file_name.back() == ']') {
                    if (frame.file_name.length() > 2) {
                        frame.file_name = frame.file_name.substr(1, frame.file_name.length() - 2);
                    } else { frame.file_name = "[unknown]"; }
                }
            } else { frame.file_name = "[unknown]"; }
            frame.vaddr_in_file = frame.ip;
            current_call_stack_data_.push_back(frame);
        } else {
             if (!line.empty() && (line[0] == '#' || line.find("PERF_RECORD_") != std::string::npos)) {
                TS_LOGI("Skipping comment or unprocessed perf record line: %s", line.c_str());
            } else {
                TS_LOGW("Skipping unmatched line (%d): %s", lineCount, line.c_str());
            }
        }
    }

    if (current_event_header_data_.pid != 0) {
        ProcessCallStack(current_event_header_data_);
    }
    current_call_stack_data_.clear();
    current_event_header_data_ = {};

    TS_LOGI("Finished parsing %d lines. Stored %zu unique callchains, %zu unique files. %zu thread entries, %zu event configs.",
            lineCount, callchain_key_to_id_.size(), file_path_id_to_file_id_.size(), known_threads_.size(), known_event_configs_.size());
    
    SdkBuffer_.clear(); // Clear buffer after processing
}

void PerfScriptParser::ProcessCallStack(const ParsedEventHeader& event_header_data) {
    if (event_header_data.pid == 0) {
        TS_LOGE("ProcessCallStack called with invalid event_header_data (PID is 0)");
        return;
    }

    uint32_t call_chain_id = 0; // 0 can signify "no callstack" or be an error, depending on schema.
                               // Here, we ensure it's set if there's a stack.

    if (current_call_stack_data_.empty()) {
        // Option 1: Define a specific call_chain_id for "empty stack" events if needed.
        // For now, if no stack, no callchain entry is made, and sample might link to call_chain_id 0.
        TS_LOGD("No call stack for PID: %u, TID: %u, TS: %" PRIu64 "ns, Event: %s",
                 event_header_data.pid, event_header_data.tid, event_header_data.internal_timestamp_,
                 event_header_data.event_name.c_str());
    } else {
        std::vector<std::tuple<uint64_t, DataIndex, uint64_t>> stack_key;
        stack_key.reserve(current_call_stack_data_.size());
        bool new_callchain_entry_needed = false;

        for (CallStackFrame& frame : current_call_stack_data_) {
            frame.symbol_id = traceDataCache_->GetDataDict().GetStringIndex(frame.symbol_name.c_str());
            frame.file_path_id = traceDataCache_->GetDataDict().GetStringIndex(frame.file_name.c_str());
            
            auto it_file = file_path_id_to_file_id_.find(frame.file_path_id);
            if (it_file == file_path_id_to_file_id_.end()) {
                frame.file_id = next_file_id_++;
                file_path_id_to_file_id_[frame.file_path_id] = frame.file_id;
            } else {
                frame.file_id = it_file->second;
            }
            stack_key.emplace_back(frame.ip, frame.symbol_id, frame.file_id);

            // Files (PerfFilesData)
            auto file_entry_key = std::make_tuple(frame.file_id, frame.symbol_id, frame.file_path_id);
            if (added_perf_files_entries_.find(file_entry_key) == added_perf_files_entries_.end()) {
                traceDataCache_->GetPerfFilesData()->AppendNewPerfFiles(frame.file_id, frame.symbol_id, frame.file_path_id);
                added_perf_files_entries_.insert(file_entry_key);
                TS_LOGD("Added PerfFile: FileID: %lu, SymbolID: %lu, FilePathID: %lu (%s @ %s)", frame.file_id, frame.symbol_id, frame.file_path_id, frame.symbol_name.c_str(), frame.file_name.c_str());
            }
        }

        auto it_chain = callchain_key_to_id_.find(stack_key);
        if (it_chain == callchain_key_to_id_.end()) {
            call_chain_id = ++next_call_chain_id_; // IDs start from 1
            callchain_key_to_id_[stack_key] = call_chain_id;
            new_callchain_entry_needed = true;
        } else {
            call_chain_id = it_chain->second;
        }

        if (new_callchain_entry_needed) {
            for (size_t depth = 0; depth < current_call_stack_data_.size(); ++depth) {
                const auto& frame = current_call_stack_data_[depth];
                traceDataCache_->GetPerfCallChainData()->AppendPerfCallChain(call_chain_id, static_cast<uint64_t>(depth), frame.ip, frame.vaddr_in_file, frame.file_id, frame.symbol_id);
                 TS_LOGD("  Added CallChain Frame: ChainID: %u, Depth: %zu, IP: %llx, Vaddr: %llx, FileID: %lu, SymID: %lu",
                          call_chain_id, depth, static_cast<unsigned long long>(frame.ip), static_cast<unsigned long long>(frame.vaddr_in_file), frame.file_id, frame.symbol_id);
            }
        }
    } // end if !current_call_stack_data_.empty()

    // Samples (PerfSampleData)
    DataIndex event_config_idx = traceDataCache_->GetDataDict().GetStringIndex(event_header_data.event_name.c_str());
    uint32_t cpu_id = 0; // Default, Perf script output doesn't usually show CPU for samples like this
    // runningStateKeyIndex_ is cached in constructor
    
    traceDataCache_->GetPerfSampleData()->AppendNewPerfSample(
        call_chain_id, // Will be 0 if stack was empty and no specific "empty stack" ID is used
        event_header_data.internal_timestamp_,
        event_header_data.tid,
        1, /*period*/
        event_config_idx,
        event_header_data.internal_timestamp_, /*ts_end - for perf, sample is usually an instant*/
        cpu_id,
        runningStateKeyIndex_
    );
    TS_LOGD("Added Sample: ChainID: %u, TS: %" PRIu64 "ns, TID: %u, Event: %s (idx: %lu), CPU: %u, State: %lu",
             call_chain_id, event_header_data.internal_timestamp_, event_header_data.tid,
             event_header_data.event_name.c_str(), event_config_idx, cpu_id, runningStateKeyIndex_);
}

} // namespace TraceStreamer
} // namespace SysTuning
