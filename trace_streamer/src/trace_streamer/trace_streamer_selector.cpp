/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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

#include "trace_streamer_selector.h"
#include <algorithm>
#include <chrono>
#include <functional>
#include <regex>
#include "animation_filter.h"
#include "app_start_filter.h"
#include "args_filter.h"
#include "binder_filter.h"
#include "clock_filter_ex.h"
#include "cpu_filter.h"
#include "file.h"
#include "filter_filter.h"
#include "frame_filter.h"
#include "hi_sysevent_measure_filter.h"
#include "irq_filter.h"
#include "measure_filter.h"
#include "task_pool_filter.h"
#include "parser/bytrace_parser/bytrace_parser.h"
#include "parser/htrace_pbreader_parser/htrace_parser.h"
#include "parser/rawtrace_parser/rawtrace_parser.h"
#include "parser/perf_script_parser.h" // Added
#include "perf_data_filter.h"
#include "process_filter.h"
#include "slice_filter.h"
#include "stat_filter.h"
#include "string_help.h"
#include "symbols_filter.h"
#include "system_event_measure_filter.h"

namespace {
const uint32_t CHUNK_SIZE = 1024 * 1024;
constexpr uint16_t RAW_TRACE_MAGIC_NUMBER = 57161;
} // namespace
using namespace SysTuning::base;
namespace SysTuning {
namespace TraceStreamer {
namespace {
TraceFileType GuessFileType(const uint8_t* data, size_t size)
{
    if (size == 0) {
        return TRACE_FILETYPE_UN_KNOW;
    }
    // Guess Perf Script by typical command line start
    // This is a heuristic and might need refinement.
    // Example: "perf script record -e cpu-clock -a -g -- sleep 1"
    // Or just the output starting with a command name.
    // A simple check for common command characters or structure.
    // This is a weak check, as bytrace can also start with text.
    // A more robust check might involve looking for the typical perf script line format.
    // For now, we'll rely on the user specifying or a stronger signal if available.
    // If the file starts with something like "my-command " or "perf ", it *could* be a perf script.
    // This particular check is very basic.
    std::string initialContent(reinterpret_cast<const char*>(data), std::min<size_t>(size, 256)); // Look at a bit more data
    // Look for a pattern like: command pid/tid timestamp: event_name
    // Example: find 4729/4729 2353410.124803: cpu-clock:
    // This regex is simplified for a quick check.
    const std::regex perfScriptLineRegex(R"((.+?)\s+(\d+)(?:/(\d+))?\s+([\d.]+):\s*(?:.+?)\s*([a-zA-Z0-9_-]+):)");
    if (std::regex_search(initialContent, perfScriptLineRegex)) {
        // Further check: if it also looks like bytrace, bytrace might take precedence
        // or we need a more specific way to distinguish.
        // For now, if it matches this, assume PERF_SCRIPT.
        // This is a placeholder for a potentially more robust detection mechanism.
        // A common issue is that Bytrace can also contain similar text patterns.
        // A dedicated file extension or metadata would be better.
        // Let's assume for now that this check is sufficient for initial testing,
        // or that the fileType_ will be set externally for TRACE_FILE_PERF_SCRIPT.
        // TS_LOGW("Perf script pattern heuristic match, considering TRACE_FILETYPE_PERF_SCRIPT");
        // return TRACE_FILETYPE_PERF_SCRIPT; // Tentatively disabled due to high false positive risk with bytrace
    }

    std::string start(reinterpret_cast<const char*>(data), std::min<size_t>(size, 20));
    if (start.find("# tracer") != std::string::npos) {
        return TRACE_FILETYPE_BY_TRACE;
    }
    if (start.find("# TRACE") != std::string::npos) {
        return TRACE_FILETYPE_BY_TRACE;
    }
    if (start.find("# SYSEVENT") != std::string::npos) {
        return TRACE_FILETYPE_SYSEVENT;
    }
    if (start.find("# sysevent") != std::string::npos) {
        return TRACE_FILETYPE_SYSEVENT;
    }
    uint16_t magicNumber = INVALID_UINT16;
    int ret = memcpy_s(&magicNumber, sizeof(uint16_t), data, sizeof(uint16_t));
    TS_CHECK_TRUE(ret == EOK, TRACE_FILETYPE_UN_KNOW, "Memcpy FAILED!Error code is %d, data size is %zu.", ret, size);
    if (magicNumber == RAW_TRACE_MAGIC_NUMBER) {
        return TRACE_FILETYPE_RAW_TRACE;
    }
    std::string lowerStart(start);
    transform(start.begin(), start.end(), lowerStart.begin(), ::tolower);
    if ((lowerStart.compare(0, std::string("<!doctype html>").length(), "<!doctype html>") == 0) ||
        (lowerStart.compare(0, std::string("<html>").length(), "<html>") == 0)) {
        return TRACE_FILETYPE_BY_TRACE;
    }
    if (start.compare(0, std::string("\x0a").length(), "\x0a") == 0) {
        return TRACE_FILETYPE_UN_KNOW;
    }
    if (start.compare(0, std::string("OHOSPROF").length(), "OHOSPROF") == 0) {
        return TRACE_FILETYPE_H_TRACE;
    }
    if (start.compare(0, std::string("PERFILE2").length(), "PERFILE2") == 0) {
        return TRACE_FILETYPE_PERF;
    }
    const std::regex bytraceMatcher = std::regex(R"(-(\d+)\s+\(?\s*(\d+|-+)?\)?\s?\[(\d+)\]\s*)"
                                                 R"([a-zA-Z0-9.]{0,5}\s+(\d+\.\d+):\s+(\S+):)");
    std::smatch matcheLine;
    std::string bytraceMode(reinterpret_cast<const char*>(data), size);
    if (std::regex_search(bytraceMode, matcheLine, bytraceMatcher)) {
        return TRACE_FILETYPE_BY_TRACE;
    }

    const std::regex hilogMatcher = std::regex(R"( *(\w+ )?([\-\d: ]+\.\d+) +(\d+) +(\d+) +([FEWID]) +(.+?): +(.+))");
    if (std::regex_search(bytraceMode, matcheLine, hilogMatcher)) {
        return TRACE_FILETYPE_HILOG;
    }

    return TRACE_FILETYPE_UN_KNOW;
}
} // namespace

TraceStreamerSelector::TraceStreamerSelector()
    : fileType_(TRACE_FILETYPE_UN_KNOW),
      bytraceParser_(nullptr),
      htraceParser_(nullptr),
      rawTraceParser_(nullptr),
      perfScriptParser_(nullptr) // Initialized to nullptr
{
    InitFilter();
    // Instantiate PerfScriptParser here as it doesn't depend on file type guess
    perfScriptParser_ = std::make_unique<PerfScriptParser>(traceDataCache_.get(), streamFilters_.get());
}
TraceStreamerSelector::~TraceStreamerSelector() {}

void TraceStreamerSelector::InitFilter()
{
    streamFilters_ = std::make_unique<TraceStreamerFilters>();
    traceDataCache_ = std::make_unique<TraceDataCache>();
    streamFilters_->animationFilter_ = std::make_unique<AnimationFilter>(traceDataCache_.get(), streamFilters_.get());
    streamFilters_->cpuFilter_ = std::make_unique<CpuFilter>(traceDataCache_.get(), streamFilters_.get());
    streamFilters_->sliceFilter_ = std::make_unique<SliceFilter>(traceDataCache_.get(), streamFilters_.get());

    streamFilters_->processFilter_ = std::make_unique<ProcessFilter>(traceDataCache_.get(), streamFilters_.get());
    streamFilters_->clockFilter_ = std::make_unique<ClockFilterEx>(traceDataCache_.get(), streamFilters_.get());
    streamFilters_->filterFilter_ = std::make_unique<FilterFilter>(traceDataCache_.get(), streamFilters_.get());

    streamFilters_->threadMeasureFilter_ =
        std::make_unique<MeasureFilter>(traceDataCache_.get(), streamFilters_.get(), E_THREADMEASURE_FILTER);
    streamFilters_->threadFilter_ =
        std::make_unique<MeasureFilter>(traceDataCache_.get(), streamFilters_.get(), E_THREAD_FILTER);
    streamFilters_->cpuMeasureFilter_ =
        std::make_unique<MeasureFilter>(traceDataCache_.get(), streamFilters_.get(), E_CPU_MEASURE_FILTER);
    streamFilters_->processMeasureFilter_ =
        std::make_unique<MeasureFilter>(traceDataCache_.get(), streamFilters_.get(), E_PROCESS_MEASURE_FILTER);
    streamFilters_->processFilterFilter_ =
        std::make_unique<MeasureFilter>(traceDataCache_.get(), streamFilters_.get(), E_PROCESS_FILTER_FILTER);
    streamFilters_->symbolsFilter_ = std::make_unique<SymbolsFilter>(traceDataCache_.get(), streamFilters_.get());
    streamFilters_->statFilter_ = std::make_unique<StatFilter>(traceDataCache_.get(), streamFilters_.get());
    streamFilters_->binderFilter_ = std::make_unique<BinderFilter>(traceDataCache_.get(), streamFilters_.get());
    streamFilters_->argsFilter_ = std::make_unique<ArgsFilter>(traceDataCache_.get(), streamFilters_.get());
    streamFilters_->irqFilter_ = std::make_unique<IrqFilter>(traceDataCache_.get(), streamFilters_.get());
    streamFilters_->frameFilter_ = std::make_unique<FrameFilter>(traceDataCache_.get(), streamFilters_.get());
    streamFilters_->clockRateFilter_ =
        std::make_unique<MeasureFilter>(traceDataCache_.get(), streamFilters_.get(), E_CLOCK_RATE_FILTER);
    streamFilters_->clockEnableFilter_ =
        std::make_unique<MeasureFilter>(traceDataCache_.get(), streamFilters_.get(), E_CLOCK_ENABLE_FILTER);
    streamFilters_->clockDisableFilter_ =
        std::make_unique<MeasureFilter>(traceDataCache_.get(), streamFilters_.get(), E_CLOCK_DISABLE_FILTER);
    streamFilters_->clkRateFilter_ =
        std::make_unique<MeasureFilter>(traceDataCache_.get(), streamFilters_.get(), E_CLK_RATE_FILTER);
    streamFilters_->clkEnableFilter_ =
        std::make_unique<MeasureFilter>(traceDataCache_.get(), streamFilters_.get(), E_CLK_ENABLE_FILTER);
    streamFilters_->clkDisableFilter_ =
        std::make_unique<MeasureFilter>(traceDataCache_.get(), streamFilters_.get(), E_CLK_DISABLE_FILTER);
    streamFilters_->sysEventMemMeasureFilter_ =
        std::make_unique<SystemEventMeasureFilter>(traceDataCache_.get(), streamFilters_.get(), E_SYS_MEMORY_FILTER);
    streamFilters_->sysEventVMemMeasureFilter_ = std::make_unique<SystemEventMeasureFilter>(
        traceDataCache_.get(), streamFilters_.get(), E_SYS_VIRTUAL_MEMORY_FILTER);
    streamFilters_->appStartupFilter_ = std::make_unique<APPStartupFilter>(traceDataCache_.get(), streamFilters_.get());
    streamFilters_->perfDataFilter_ = std::make_unique<PerfDataFilter>(traceDataCache_.get(), streamFilters_.get());
    streamFilters_->sysEventSourceFilter_ = std::make_unique<SystemEventMeasureFilter>(
        traceDataCache_.get(), streamFilters_.get(), E_SYS_EVENT_SOURCE_FILTER);
    streamFilters_->hiSysEventMeasureFilter_ =
        std::make_unique<HiSysEventMeasureFilter>(traceDataCache_.get(), streamFilters_.get());
    streamFilters_->taskPoolFilter_ = std::make_unique<TaskPoolFilter>(traceDataCache_.get(), streamFilters_.get());
}

void TraceStreamerSelector::WaitForParserEnd()
{
    if (fileType_ == TRACE_FILETYPE_H_TRACE) {
        htraceParser_->WaitForParserEnd();
    }
    if (fileType_ == TRACE_FILETYPE_BY_TRACE || fileType_ == TRACE_FILETYPE_HILOG) {
        bytraceParser_->WaitForParserEnd();
    }
    if (fileType_ == TRACE_FILETYPE_PERF) {
        htraceParser_->TraceDataSegmentEnd(false);
        htraceParser_->WaitForParserEnd();
    }
    if (fileType_ == TRACE_FILETYPE_RAW_TRACE) {
        rawTraceParser_->WaitForParserEnd();
    }
    traceDataCache_->UpdateTraceRange();
    if (traceDataCache_->AnimationTraceEnabled()) {
        streamFilters_->animationFilter_->UpdateFrameInfo();
        streamFilters_->animationFilter_->UpdateDynamicFrameInfo();
    }
}

MetaData* TraceStreamerSelector::GetMetaData()
{
    return traceDataCache_->GetMetaData();
}

void TraceStreamerSelector::SetDataType(TraceFileType type)
{
    fileType_ = type;
    if (fileType_ == TRACE_FILETYPE_H_TRACE) {
        htraceParser_ = std::make_unique<HtraceParser>(traceDataCache_.get(), streamFilters_.get());
    } else if (fileType_ == TRACE_FILETYPE_BY_TRACE) {
        bytraceParser_ = std::make_unique<BytraceParser>(traceDataCache_.get(), streamFilters_.get());
    } else if (fileType_ == TRACE_FILETYPE_RAW_TRACE) {
        rawTraceParser_ = std::make_unique<RawTraceParser>(traceDataCache_.get(), streamFilters_.get());
    }
}
bool TraceStreamerSelector::ParseTraceDataSegment(std::unique_ptr<uint8_t[]> data,
                                                  size_t size,
                                                  bool isSplitFile,
                                                  int32_t isFinish)
{
    if (size == 0) {
        return true;
    }
    if (fileType_ == TRACE_FILETYPE_UN_KNOW) {
        fileType_ = GuessFileType(data.get(), size);
        // NOTE: PerfScriptParser is already created in the constructor.
        // We only need to create other parsers if their type is guessed.
        if (fileType_ == TRACE_FILETYPE_H_TRACE || fileType_ == TRACE_FILETYPE_PERF) {
            htraceParser_ = std::make_unique<HtraceParser>(traceDataCache_.get(), streamFilters_.get());
            htraceParser_->EnableFileSeparate(enableFileSeparate_);
        } else if (fileType_ == TRACE_FILETYPE_BY_TRACE || fileType_ == TRACE_FILETYPE_SYSEVENT ||
                   fileType_ == TRACE_FILETYPE_HILOG) {
            bytraceParser_ = std::make_unique<BytraceParser>(traceDataCache_.get(), streamFilters_.get(), fileType_);
            bytraceParser_->EnableBytrace(fileType_ == TRACE_FILETYPE_BY_TRACE);
        } else if (fileType_ == TRACE_FILETYPE_RAW_TRACE) {
            rawTraceParser_ = std::make_unique<RawTraceParser>(traceDataCache_.get(), streamFilters_.get());
        } else if (fileType_ == TRACE_FILETYPE_PERF_SCRIPT) {
            // PerfScriptParser already created, nothing to do here for it
            TS_LOGI("File type identified as Perf Script.");
        }
        // This TRACE_FILETYPE_UN_KNOW check should ideally be after all specific type checks
        if (fileType_ == TRACE_FILETYPE_UN_KNOW) {
            SetAnalysisResult(TRACE_PARSER_FILE_TYPE_ERROR);
            TS_LOGI(
                "File type is not supported!,\nthe head content is:%s\n ---warning!!!---\n"
                "File type is not supported!,\n",
                data.get());
            return false;
        }
    }
    traceDataCache_->SetSplitFileMinTime(minTs_);
    traceDataCache_->SetSplitFileMaxTime(maxTs_);
    traceDataCache_->isSplitFile_ = isSplitFile;
    if (fileType_ == TRACE_FILETYPE_H_TRACE) {
        htraceParser_->ParseTraceDataSegment(std::move(data), size);
    } else if (fileType_ == TRACE_FILETYPE_BY_TRACE || fileType_ == TRACE_FILETYPE_SYSEVENT ||
               fileType_ == TRACE_FILETYPE_HILOG) {
        bytraceParser_->ParseTraceDataSegment(std::move(data), size);
        return true;
    } else if (fileType_ == TRACE_FILETYPE_PERF) {
        htraceParser_->StoreTraceDataSegment(std::move(data), size, isFinish);
    } else if (fileType_ == TRACE_FILETYPE_RAW_TRACE) {
        rawTraceParser_->ParseTraceDataSegment(std::move(data), size);
    } else if (fileType_ == TRACE_FILETYPE_PERF_SCRIPT) {
        // Assuming data is char* for PerfScriptParser. If it's uint8_t*, cast is needed.
        // The ParsePerfScript method needs to be adapted for segmented data.
        // The current PerfScriptParser::ParsePerfScript expects a single std::string.
        // This will be addressed in Step 5.
        // For now, let's call a placeholder or the modified signature.
        perfScriptParser_->ParsePerfScript(reinterpret_cast<const char*>(data.get()), size, isFinish != 0);
    }
    SetAnalysisResult(TRACE_PARSER_NORMAL);
    return true;
}
void TraceStreamerSelector::EnableMetaTable(bool enabled)
{
    traceDataCache_->EnableMetaTable(enabled);
}

void TraceStreamerSelector::EnableFileSave(bool enabled)
{
    enableFileSeparate_ = enabled;
}

void TraceStreamerSelector::SetCleanMode(bool cleanMode)
{
    g_cleanMode = true;
}

int32_t TraceStreamerSelector::ExportDatabase(const std::string& outputName, TraceDataDB::ResultCallBack resultCallBack)
{
    traceDataCache_->UpdateTraceRange();
    return traceDataCache_->ExportDatabase(outputName, resultCallBack);
}

bool TraceStreamerSelector::ReloadSymbolFiles(std::string& directory, std::vector<std::string>& symbolsPaths)
{
    if (fileType_ != TRACE_FILETYPE_H_TRACE) {
        return false;
    }
    TS_LOGE("directory is %s", directory.c_str());
    for (auto file : symbolsPaths) {
        TS_LOGE("files is %s", file.c_str());
    }
    return htraceParser_->ReparseSymbolFilesAndResymbolization(directory, symbolsPaths);
}
void TraceStreamerSelector::Clear()
{
    traceDataCache_->Prepare();
    traceDataCache_->Clear();
}
std::vector<std::string> TraceStreamerSelector::SearchData()
{
    return traceDataCache_->SearchData();
}
int32_t TraceStreamerSelector::OperateDatabase(const std::string& sql)
{
    return traceDataCache_->OperateDatabase(sql);
}
int32_t TraceStreamerSelector::SearchDatabase(const std::string& sql, TraceDataDB::ResultCallBack resultCallBack)
{
    return traceDataCache_->SearchDatabase(sql, resultCallBack);
}
int32_t TraceStreamerSelector::SearchDatabase(const std::string& sql, uint8_t* out, int32_t outLen)
{
    return traceDataCache_->SearchDatabase(sql, out, outLen);
}
int32_t TraceStreamerSelector::SearchDatabase(std::string& sql, bool printf)
{
    return traceDataCache_->SearchDatabase(sql, printf);
}
std::string TraceStreamerSelector::SearchDatabase(const std::string& sql)
{
    return traceDataCache_->SearchDatabase(sql);
}
void TraceStreamerSelector::InitMetricsMap(std::map<std::string, std::string>& metricsMap)
{
    metricsMap.emplace(TRACE_MEM_UNAGG, memUnaggQuery);
    metricsMap.emplace(TRACE_MEM, memQuery);
    metricsMap.emplace(TRACE_MEM_TOP_TEN, memTopQuery);
    metricsMap.emplace(TRACE_METADATA, metaDataQuery);
    metricsMap.emplace(SYS_CALLS, sysCallQuery);
    metricsMap.emplace(TRACE_STATS, traceStateQuery);
    metricsMap.emplace(TRACE_TASK_NAMES, traceTaskName);
}
const std::string TraceStreamerSelector::MetricsSqlQuery(const std::string& metrics)
{
    std::map<std::string, std::string> metricsMap;
    InitMetricsMap(metricsMap);
    auto itor = metricsMap.find(metrics);
    if (itor == metricsMap.end()) {
        TS_LOGE("metrics name error!!!");
        return "";
    }
    return itor->second;
}
int32_t TraceStreamerSelector::UpdateTraceRangeTime(uint8_t* data, int32_t len)
{
    std::string traceRangeStr;
    memcpy(&traceRangeStr, data, len);
    std::vector<string> vTraceRangeStr = SplitStringToVec(traceRangeStr, ";");
    uint64_t minTs = std::stoull(vTraceRangeStr.at(0));
    uint64_t maxTs = std::stoull(vTraceRangeStr.at(1));
    traceDataCache_->UpdateTraceTime(minTs);
    traceDataCache_->UpdateTraceTime(maxTs);
    return 0;
}
void TraceStreamerSelector::SetCancel(bool cancel)
{
    traceDataCache_->SetCancel(cancel);
}
void TraceStreamerSelector::UpdateBinderRunnableTraceStatus(bool status)
{
    traceDataCache_->UpdateBinderRunnableTraceStatus(status);
}
void TraceStreamerSelector::UpdateAnimationTraceStatus(bool status)
{
    traceDataCache_->UpdateAnimationTraceStatus(status);
}
void TraceStreamerSelector::UpdateTaskPoolTraceStatus(bool status)
{
    traceDataCache_->UpdateTaskPoolTraceStatus(status);
}
void TraceStreamerSelector::UpdateAppStartTraceStatus(bool status)
{
    traceDataCache_->UpdateAppStartTraceStatus(status);
}
bool TraceStreamerSelector::LoadQueryFile(const std::string& sqlOperator, std::vector<std::string>& sqlStrings)
{
    auto fd = fopen(sqlOperator.c_str(), "r");
    if (!fd) {
        TS_LOGE("open file failed!");
        return false;
    }
    char buffer[CHUNK_SIZE];
    while (!feof(fd)) {
        std::string sqlString;
        while (fgets(buffer, sizeof(buffer), fd)) {
            std::string line = buffer;
            if (line == "\n" || line == "\r\n") {
                break;
            }
            sqlString.append(buffer);

            if (EndWith(line, ";") || EndWith(line, ";\r\n")) {
                break;
            }
        }

        if (sqlString.empty()) {
            continue;
        }
        sqlStrings.push_back(sqlString);
    }
    fclose(fd);
    fd = nullptr;
    return true;
}
bool TraceStreamerSelector::ReadSqlFileAndPrintResult(const std::string& sqlOperator)
{
    std::vector<std::string> sqlStrings;
    if (!LoadQueryFile(sqlOperator, sqlStrings)) {
        return false;
    }
    for (auto& str : sqlStrings) {
        SearchDatabase(str, true);
    }
    return true;
}
bool TraceStreamerSelector::ParserAndPrintMetrics(const std::string& metrics)
{
    auto metricsName = SplitStringToVec(metrics, ",");
    for (const auto& itemName : metricsName) {
        std::string result = SearchDatabase(MetricsSqlQuery(itemName));
        if (result == "") {
            return false;
        }
        Metrics metricsOperator;
        metricsOperator.ParserJson(itemName, result);
        for (auto item : metricsOperator.GetMetricsMap()) {
            if (item.second == itemName) {
                metricsOperator.PrintMetricsResult(item.first, nullptr);
                continue;
            }
        }
    }
    return true;
}
} // namespace TraceStreamer
} // namespace SysTuning
