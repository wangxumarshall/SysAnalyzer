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
#include "ebpf_elf_table.h"

namespace SysTuning {
namespace TraceStreamer {
enum class Index : int32_t {
    ID = 0,
    ELF_ID,
    TEXT_VADDR,
    TEXT_OFFSET,
    STR_TAB_LEN,
    SYM_TAB_LEN,
    FILE_NAME_LEN,
    SYM_ENT_LEN,
    FILE_PATH_ID,
};
EbpfElfTable::EbpfElfTable(const TraceDataCache* dataCache) : TableBase(dataCache)
{
    tableColumn_.push_back(TableBase::ColumnInfo("id", "INTEGER"));
    tableColumn_.push_back(TableBase::ColumnInfo("elf_id", "INTEGER"));
    tableColumn_.push_back(TableBase::ColumnInfo("text_vaddr", "INTEGER"));
    tableColumn_.push_back(TableBase::ColumnInfo("text_offset", "INTEGER"));
    tableColumn_.push_back(TableBase::ColumnInfo("str_tab_len", "INTEGER"));
    tableColumn_.push_back(TableBase::ColumnInfo("sym_tab_len", "INTEGER"));
    tableColumn_.push_back(TableBase::ColumnInfo("file_name_len", "INTEGER"));
    tableColumn_.push_back(TableBase::ColumnInfo("sym_ent_len", "INTEGER"));
    tableColumn_.push_back(TableBase::ColumnInfo("file_path_id", "INTEGER"));
    tablePriKey_.push_back("id");
}

EbpfElfTable::~EbpfElfTable() {}

void EbpfElfTable::EstimateFilterCost(FilterConstraints& fc, EstimatedIndexInfo& ei)
{
    constexpr double filterBaseCost = 1000.0; // set-up and tear-down
    constexpr double indexCost = 2.0;
    ei.estimatedCost = filterBaseCost;

    auto rowCount = dataCache_->GetConstHidumpData().Size();
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
                break;
            default: // other columns can be sorted by SQLite
                ei.isOrdered = false;
                break;
        }
    }
}

void EbpfElfTable::FilterByConstraint(FilterConstraints& fc, double& filterCost, size_t rowCount)
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
            default:                    // other column
                filterCost += rowCount; // scan all rows
                break;
        }
    }
}

std::unique_ptr<TableBase::Cursor> EbpfElfTable::CreateCursor()
{
    return std::make_unique<Cursor>(dataCache_, this);
}

EbpfElfTable::Cursor::Cursor(const TraceDataCache* dataCache, TableBase* table)
    : TableBase::Cursor(dataCache, table, static_cast<uint32_t>(dataCache->GetConstEbpfElf().Size())),
      ebpfElfObj_(dataCache->GetConstEbpfElf())
{
}

EbpfElfTable::Cursor::~Cursor() {}

int32_t EbpfElfTable::Cursor::Column(int32_t column) const
{
    switch (static_cast<Index>(column)) {
        case Index::ID:
            sqlite3_result_int64(context_, static_cast<int32_t>(ebpfElfObj_.IdsData()[CurrentRow()]));
            break;
        case Index::ELF_ID:
            sqlite3_result_int64(context_, static_cast<int64_t>(ebpfElfObj_.ElfIds()[CurrentRow()]));
            break;
        case Index::TEXT_VADDR:
            sqlite3_result_int64(context_, static_cast<int64_t>(ebpfElfObj_.TextVaddrs()[CurrentRow()]));
            break;
        case Index::TEXT_OFFSET:
            sqlite3_result_int64(context_, static_cast<int64_t>(ebpfElfObj_.TextOffsets()[CurrentRow()]));
            break;
        case Index::STR_TAB_LEN:
            sqlite3_result_int64(context_, static_cast<int64_t>(ebpfElfObj_.StrTabLens()[CurrentRow()]));
            break;
        case Index::SYM_TAB_LEN: {
            if (ebpfElfObj_.SymTabLens()[CurrentRow()] != INVALID_UINT32) {
                sqlite3_result_int64(context_, static_cast<int64_t>(ebpfElfObj_.SymTabLens()[CurrentRow()]));
            }
            break;
        }
        case Index::FILE_NAME_LEN: {
            if (ebpfElfObj_.FileNameLens()[CurrentRow()] != INVALID_UINT32) {
                sqlite3_result_int64(context_, static_cast<int64_t>(ebpfElfObj_.FileNameLens()[CurrentRow()]));
            }
            break;
        }
        case Index::SYM_ENT_LEN: {
            if (ebpfElfObj_.SymEntLens()[CurrentRow()] != INVALID_UINT32) {
                sqlite3_result_int64(context_, static_cast<int64_t>(ebpfElfObj_.SymEntLens()[CurrentRow()]));
            }
            break;
        }
        case Index::FILE_PATH_ID: {
            if (ebpfElfObj_.FileNameIndexs()[CurrentRow()] != INVALID_UINT64) {
                sqlite3_result_int64(context_, static_cast<int64_t>(ebpfElfObj_.FileNameIndexs()[CurrentRow()]));
            }
            break;
        }
        default:
            TS_LOGF("Unregistered column : %d", column);
            break;
    }
    return SQLITE_OK;
}
} // namespace TraceStreamer
} // namespace SysTuning
