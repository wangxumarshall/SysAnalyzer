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

#ifndef TRACE_STATE_STRATEGY_H
#define TRACE_STATE_STRATEGY_H
#include <map>
#include <string>
#include <vector>

namespace SysTuning {
namespace TraceStreamer {
struct StatItem {
    std::string name;
    uint32_t count;
    std::string source;
    std::string severity;
};
} // namespace TraceStreamer
} // namespace SysTuning

#endif // TRACE_STATE_STRATEGY_H
