/*
 * Copyright (C) 2022 Huawei Device Co., Ltd.
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

// @ts-ignore
import { SpFileSystemChart } from '../../../../dist/trace/component/chart/SpFileSystemChart.js';
// @ts-ignore
import { SpChartManager } from '../../../../dist/trace/component/chart/SpChartManager.js';
const sqlit = require('../../../../dist/trace/database/SqlLite.js');
jest.mock('../../../../dist/trace/database/SqlLite.js');
// @ts-ignore
import { TraceRow } from '../../../../dist/trace/component/trace/base/TraceRow.js';
// @ts-ignore
import { SpSystemTrace } from '../../../../dist/trace/component/SpSystemTrace.js';
jest.mock('../../../../dist/js-heap/model/DatabaseStruct.js', () => {});
window.ResizeObserver =
  window.ResizeObserver ||
  jest.fn().mockImplementation(() => ({
    disconnect: jest.fn(),
    observe: jest.fn(),
    unobserve: jest.fn(),
  }));

describe('SpFileSystemChart Test', () => {
  let hasFileSysData = sqlit.hasFileSysData;
  hasFileSysData.mockResolvedValue([
    {
      fsCount: 2,
      vmCount: 2,
      ioCount: 2,
    },
  ]);

  let ss = new SpChartManager();
  let spFileSystemChart = new SpFileSystemChart(ss);
  spFileSystemChart.initFileCallchain = jest.fn(() => true);
  it('SpMpsChart01', function () {
    spFileSystemChart.init();
    expect(spFileSystemChart).toBeDefined();
  });
  it('SpMpsChart02', function () {
    ss.displayTip = jest.fn(() => true);
    expect(spFileSystemChart.focusHandler(TraceRow)).toBeUndefined();
  });
});
