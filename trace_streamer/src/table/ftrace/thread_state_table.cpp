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

#include "thread_state_table.h"
#include "thread_state_flag.h"

#include <cmath>

namespace SysTuning {
namespace TraceStreamer {
enum class Index : int32_t { ID = 0, TYPE, TS, DUR, CPU, INTERNAL_TID, TID, PID, STATE, ARGSETID };
ThreadStateTable::ThreadStateTable(const TraceDataCache* dataCache) : TableBase(dataCache)
{
    tableColumn_.push_back(TableBase::ColumnInfo("id", "INTEGER"));
    tableColumn_.push_back(TableBase::ColumnInfo("type", "TEXT"));
    tableColumn_.push_back(TableBase::ColumnInfo("ts", "INTEGER"));
    tableColumn_.push_back(TableBase::ColumnInfo("dur", "INTEGER"));
    tableColumn_.push_back(TableBase::ColumnInfo("cpu", "INTEGER"));
    tableColumn_.push_back(TableBase::ColumnInfo("itid", "INTEGER"));
    tableColumn_.push_back(TableBase::ColumnInfo("tid", "INTEGER"));
    tableColumn_.push_back(TableBase::ColumnInfo("pid", "INTEGER"));
    tableColumn_.push_back(TableBase::ColumnInfo("state", "TEXT"));
    tableColumn_.push_back(TableBase::ColumnInfo("arg_setid", "INTEGER"));
    tablePriKey_.push_back("id");
}

ThreadStateTable::~ThreadStateTable() {}

void ThreadStateTable::EstimateFilterCost(FilterConstraints& fc, EstimatedIndexInfo& ei)
{
    constexpr double filterBaseCost = 1000.0; // set-up and tear-down
    constexpr double indexCost = 2.0;
    ei.estimatedCost = filterBaseCost;

    auto rowCount = dataCache_->GetConstThreadStateData().Size();
    if (rowCount == 0 || rowCount == 1) {
        ei.estimatedRows = rowCount;
        ei.estimatedCost += indexCost * rowCount;
        return;
    }

    double filterCost = 0.0;
    auto constraints = fc.GetConstraints();
    if (constraints.empty()) { // scan all rows
        filterCost = rowCount;
    } else {
        FilterByConstraint(fc, filterCost, rowCount);
    }
    ei.estimatedCost += filterCost;
    ei.estimatedRows = rowCount;
    ei.estimatedCost += rowCount * indexCost;

    ei.isOrdered = true;
    auto orderbys = fc.GetOrderBys();
    for (auto i = 0; i < orderbys.size(); i++) {
        switch (static_cast<Index>(orderbys[i].iColumn)) {
            case Index::ID:
            case Index::TS:
                break;
            default: // other columns can be sorted by SQLite
                ei.isOrdered = false;
                break;
        }
    }
}

void ThreadStateTable::FilterByConstraint(FilterConstraints& fc, double& filterCost, size_t rowCount)
{
    auto fcConstraints = fc.GetConstraints();
    for (int32_t i = 0; i < static_cast<int32_t>(fcConstraints.size()); i++) {
        if (rowCount <= 1) {
            // only one row or nothing, needn't filter by constraint
            filterCost += rowCount;
            break;
        }
        const auto& c = fcConstraints[i];
        switch (static_cast<Index>(c.col)) {
            case Index::ID: {
                if (CanFilterId(c.op, rowCount)) {
                    fc.UpdateConstraint(i, true);
                    filterCost += 1; // id can position by 1 step
                } else {
                    filterCost += rowCount; // scan all rows
                }
                break;
            }
            case Index::TS: {
                auto oldRowCount = rowCount;
                if (CanFilterSorted(c.op, rowCount)) {
                    fc.UpdateConstraint(i, true);
                    filterCost += log2(oldRowCount); // binary search
                } else {
                    filterCost += oldRowCount;
                }
                break;
            }
            default:                    // other column
                filterCost += rowCount; // scan all rows
                break;
        }
    }
}

bool ThreadStateTable::CanFilterSorted(const char op, size_t& rowCount) const
{
    switch (op) {
        case SQLITE_INDEX_CONSTRAINT_EQ:
            rowCount = rowCount / log2(rowCount);
            break;
        case SQLITE_INDEX_CONSTRAINT_GT:
        case SQLITE_INDEX_CONSTRAINT_GE:
        case SQLITE_INDEX_CONSTRAINT_LE:
        case SQLITE_INDEX_CONSTRAINT_LT:
            rowCount = (rowCount >> 1);
            break;
        default:
            return false;
    }
    return true;
}

std::unique_ptr<TableBase::Cursor> ThreadStateTable::CreateCursor()
{
    return std::make_unique<Cursor>(dataCache_, this);
}

ThreadStateTable::Cursor::Cursor(const TraceDataCache* dataCache, TableBase* table)
    : TableBase::Cursor(dataCache, table, dataCache->GetConstThreadStateData().Size()),
      threadStateObj_(dataCache->GetConstThreadStateData())
{
}

ThreadStateTable::Cursor::~Cursor() {}

int32_t ThreadStateTable::Cursor::Filter(const FilterConstraints& fc, sqlite3_value** argv)
{
    // reset
    if (rowCount_ <= 0) {
        return SQLITE_OK;
    }
    IndexMap* indexMapBack = indexMap_.get();
    if (indexMap_->HasData()) {
        indexMapBack = std::make_unique<IndexMap>(0, rowCount_).get();
    }
    auto& cs = fc.GetConstraints();
    for (size_t i = 0; i < cs.size(); i++) {
        const auto& c = cs[i];
        switch (static_cast<Index>(c.col)) {
            case Index::ID:
                indexMapBack->FilterId(c.op, argv[i]);
                break;
            case Index::TS:
                indexMapBack->FilterTS(c.op, argv[i], threadStateObj_.TimeStamsData());
                break;
            case Index::INTERNAL_TID:
                indexMapBack->MixRange(c.op, static_cast<uint32_t>(sqlite3_value_int(argv[i])),
                                       threadStateObj_.ItidsData());
                break;
            case Index::TID:
                indexMapBack->MixRange(c.op, static_cast<uint32_t>(sqlite3_value_int(argv[i])),
                                       threadStateObj_.TidsData());
                break;
            case Index::PID:
                indexMapBack->MixRange(c.op, static_cast<uint32_t>(sqlite3_value_int(argv[i])),
                                       threadStateObj_.PidsData());
                break;
            case Index::DUR:
                indexMapBack->MixRange(c.op, static_cast<uint64_t>(sqlite3_value_int64(argv[i])),
                                       threadStateObj_.DursData());
                break;
            case Index::CPU:
                indexMapBack->MixRange(c.op, static_cast<uint32_t>(sqlite3_value_int(argv[i])),
                                       threadStateObj_.CpusData());
                break;
            case Index::STATE:
                indexMapBack->MixRange(c.op,
                                       static_cast<DataIndex>(dataCache_->GetThreadStateValue(
                                           std::string(reinterpret_cast<const char*>(sqlite3_value_text(argv[i]))))),
                                       threadStateObj_.StatesData());
                break;
            case Index::ARGSETID:
                indexMapBack->MixRange(c.op, static_cast<uint32_t>(sqlite3_value_int(argv[i])),
                                       threadStateObj_.ArgSetsData());
                break;
            default:
                break;
        }
    }
    if (indexMap_->HasData()) {
        indexMap_->Merge(indexMapBack);
    }

    auto orderbys = fc.GetOrderBys();
    for (auto i = orderbys.size(); i > 0;) {
        i--;
        switch (static_cast<Index>(orderbys[i].iColumn)) {
            case Index::ID:
            case Index::TS:
                indexMap_->SortBy(orderbys[i].desc);
                break;
            default:
                break;
        }
    }

    return SQLITE_OK;
}

int32_t ThreadStateTable::Cursor::Column(int32_t col) const
{
    switch (static_cast<Index>(col)) {
        case Index::ID:
            sqlite3_result_int64(context_, static_cast<sqlite3_int64>(CurrentRow()));
            break;
        case Index::TYPE:
            sqlite3_result_text(context_, "thread_state", STR_DEFAULT_LEN, nullptr);
            break;
        case Index::TS:
            sqlite3_result_int64(context_, static_cast<sqlite3_int64>(threadStateObj_.TimeStamsData()[CurrentRow()]));
            break;
        case Index::DUR:
            sqlite3_result_int64(context_, static_cast<sqlite3_int64>(threadStateObj_.DursData()[CurrentRow()]));
            break;
        case Index::CPU:
            if (threadStateObj_.CpusData()[CurrentRow()] != INVALID_CPU) {
                sqlite3_result_int64(context_, static_cast<sqlite3_int64>(threadStateObj_.CpusData()[CurrentRow()]));
            }
            break;
        case Index::INTERNAL_TID:
            sqlite3_result_int64(context_, static_cast<sqlite3_int64>(threadStateObj_.ItidsData()[CurrentRow()]));
            break;
        case Index::TID:
            sqlite3_result_int64(context_, static_cast<sqlite3_int64>(threadStateObj_.TidsData()[CurrentRow()]));
            break;
        case Index::PID:
            sqlite3_result_int64(context_, static_cast<sqlite3_int64>(threadStateObj_.PidsData()[CurrentRow()]));
            break;
        case Index::STATE: {
            const std::string& str = dataCache_->GetConstSchedStateData(threadStateObj_.StatesData()[CurrentRow()]);
            sqlite3_result_text(context_, str.c_str(), STR_DEFAULT_LEN, nullptr);
            break;
        }
        case Index::ARGSETID:
            if (threadStateObj_.ArgSetsData()[CurrentRow()] != INVALID_UINT32) {
                sqlite3_result_int64(context_, static_cast<sqlite3_int64>(threadStateObj_.ArgSetsData()[CurrentRow()]));
            }
            break;
        default:
            TS_LOGF("Unregistered column : %d", col);
            break;
    }
    return SQLITE_OK;
}
} // namespace TraceStreamer
} // namespace SysTuning
