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
//@ts-ignore
import { Top20ProcessThreadCount } from '../../../../dist/trace/component/schedulingAnalysis/Top20ProcessThreadCount.js';
// @ts-ignore
window.ResizeObserver =
  window.ResizeObserver ||
  jest.fn().mockImplementation(() => ({
    disconnect: jest.fn(),
    observe: jest.fn(),
    unobserve: jest.fn(),
  }));

describe('Top20ProcessSwitchCount Test', () => {
  it('Top20ProcessSwitchCountTest01', () => {
    let top20ProcessThreadCount = new Top20ProcessThreadCount();
    expect(top20ProcessThreadCount).not.toBeUndefined();
  });
  it('Top20ProcessSwitchCountTest02', () => {
    let top20ProcessThreadCount = new Top20ProcessThreadCount();
    expect(
      top20ProcessThreadCount.sortByColumn({
        key: 'number',
      })
    ).toBeUndefined();
  });
  it('Top20ProcessSwitchCountTest03', () => {
    let top20ProcessThreadCount = new Top20ProcessThreadCount();
    top20ProcessThreadCount.queryLogicWorker = jest.fn();
    expect(top20ProcessThreadCount.queryLogicWorker('', '', {})).toBeUndefined();
  });
    it('Top20ProcessSwitchCountTest04', () => {
        let top20ProcessThreadCount = new Top20ProcessThreadCount();
        top20ProcessThreadCount.queryLogicWorker = jest.fn();
        top20ProcessThreadCount.processThreadCountTbl = jest.fn();
        top20ProcessThreadCount.processThreadCountTbl.recycleDataSource = jest.fn();
        expect(top20ProcessThreadCount.init()).toBeUndefined();
    });
});
