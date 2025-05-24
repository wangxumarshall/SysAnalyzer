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

#include "gtest/gtest.h"
#include "parser/perf_script_parser.h"
#include "trace_data/trace_data_cache.h"
#include "trace_streamer_filters.h"
#include "base/ts_common.h"
#include <memory> // For std::unique_ptr

namespace SysTuning {
namespace TraceStreamer {

class PerfScriptParserTest : public ::testing::Test {
public:
    void SetUp() override
    {
        // Initialize TraceDataCache, TraceStreamerFilters, and PerfScriptParser
        traceDataCache_ = std::make_unique<TraceDataCache>();
        streamFilters_ = std::make_unique<TraceStreamerFilters>();
        streamFilters_->clockFilter_ = std::make_unique<ClockFilterEx>(traceDataCache_.get(), streamFilters_.get());
         // Initialize other filters if PerfScriptParser depends on them indirectly via TraceDataCache or common filter paths
        perfScriptParser_ = std::make_unique<PerfScriptParser>(traceDataCache_.get(), streamFilters_.get());
    }

    void TearDown() override
    {
        perfScriptParser_.reset();
        streamFilters_.reset();
        traceDataCache_.reset();
    }

protected:
    std::unique_ptr<TraceDataCache> traceDataCache_;
    std::unique_ptr<TraceStreamerFilters> streamFilters_;
    std::unique_ptr<PerfScriptParser> perfScriptParser_;
};

// Test case for a single perf event without a call stack
TEST_F(PerfScriptParserTest, ParseSingleEventNoStack)
{
    const std::string perfScriptOutput = 
        "java 25630 2431564.796425: cycles: \n"
        "\n"; // Added newline for realistic input

    perfScriptParser_->ParsePerfScript(perfScriptOutput.c_str(), perfScriptOutput.length(), true);

    // Verify PerfSampleData
    const auto& samples = traceDataCache_->GetPerfSampleData();
    ASSERT_EQ(samples.Size(), 1);
    EXPECT_EQ(samples.CallChainIds()[0], 0); // No call stack, so callChainId should be 0 or a specific "no stack" ID
    EXPECT_EQ(samples.TimeStamps()[0], 2431564796425); // Timestamp in nanoseconds
    EXPECT_EQ(samples.Tids()[0], 25630);
    
    DataIndex eventNameIndex = traceDataCache_->GetDataDict().GetStringIndex("cycles");
    EXPECT_EQ(samples.EventCounts()[0], eventNameIndex); // Should be EventName Index

    // Verify PerfThreadData
    const auto& threads = traceDataCache_->GetPerfThreadData();
    ASSERT_EQ(threads.Size(), 1);
    EXPECT_EQ(threads.Pids()[0], 25630); // Assuming PID is same as TID if not specified otherwise
    EXPECT_EQ(threads.Tids()[0], 25630);
    DataIndex cmdIndex = traceDataCache_->GetDataDict().GetStringIndex("java");
    EXPECT_EQ(threads.ThreadNames()[0], cmdIndex);

    // Verify PerfReportData (event name config)
    const auto& reports = traceDataCache_->GetPerfReportData();
    bool eventConfigFound = false;
    for (size_t i = 0; i < reports.Size(); ++i) {
        if (reports.Values()[i] == eventNameIndex) {
            eventConfigFound = true;
            break;
        }
    }
    EXPECT_TRUE(eventConfigFound);
}

// Test case for an event with a call stack
TEST_F(PerfScriptParserTest, ParseEventWithStack)
{
    const std::string perfScriptOutput =
        "VsyncThread 1501/1501 2431564.800123: cpu-clock:\n"
        "    ffffffffc0401234 native_write_msr+0x4 ([kernel.kallsyms])\n"
        "    ffffffffc0a05678 do_syscall_64+0x58 ([kernel.kallsyms])\n"
        "\n";

    perfScriptParser_->ParsePerfScript(perfScriptOutput.c_str(), perfScriptOutput.length(), true);

    // Verify PerfSampleData
    const auto& samples = traceDataCache_->GetPerfSampleData();
    ASSERT_EQ(samples.Size(), 1);
    EXPECT_NE(samples.CallChainIds()[0], 0); // Should have a valid callChainId
    EXPECT_NE(samples.CallChainIds()[0], INVALID_CALL_CHAIN_ID);
    uint32_t callChainId = samples.CallChainIds()[0];

    // Verify PerfCallChainData
    const auto& callChains = traceDataCache_->GetPerfCallChainData();
    size_t frameCount = 0;
    for (size_t i = 0; i < callChains.Size(); ++i) {
        if (callChains.CallChainIds()[i] == callChainId) {
            frameCount++;
        }
    }
    ASSERT_EQ(frameCount, 2);

    // Verify the first frame
    bool firstFrameFound = false;
    for (size_t i = 0; i < callChains.Size(); ++i) {
        if (callChains.CallChainIds()[i] == callChainId && callChains.Depths()[i] == 0) {
            firstFrameFound = true;
            EXPECT_EQ(callChains.Ips()[i], 0xffffffffc0401234);
            
            DataIndex expectedSymbolIndex = traceDataCache_->GetDataDict().GetStringIndex("native_write_msr+0x4");
            EXPECT_EQ(callChains.SymbolIds()[i], expectedSymbolIndex);

            DataIndex expectedPathIndex = traceDataCache_->GetDataDict().GetStringIndex("kernel.kallsyms");
            
            // Find the FileId associated with this path_id
            const auto& perfFiles = traceDataCache_->GetPerfFilesData();
            uint64_t expectedFileId = INVALID_UINT64;
            for(size_t f = 0; f < perfFiles.Size(); ++f) {
                if (perfFiles.PathIds()[f] == expectedPathIndex) {
                    expectedFileId = perfFiles.FileIds()[f];
                    break;
                }
            }
            ASSERT_NE(expectedFileId, INVALID_UINT64);
            EXPECT_EQ(callChains.FileIds()[i], expectedFileId);
            break;
        }
    }
    EXPECT_TRUE(firstFrameFound);
}

} // namespace TraceStreamer
} // namespace SysTuning
