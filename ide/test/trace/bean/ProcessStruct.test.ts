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

jest.mock('../../../dist/trace/component/trace/base/TraceRow.js', () => {
  return {};
});

// @ts-ignore
import { ProcessStruct } from '../../../dist/trace/bean/ProcessStruct.js';

jest.mock('../../../dist/trace/database/ui-worker/ProcedureWorker.js', () => {
  return {};
});

describe('ProcessStruct Test', () => {
  const canvas = document.createElement('canvas');
  canvas.width = 2;
  canvas.height = 1;
  const ctx = canvas.getContext('2d');

  const data = {
    frame: {
      x: 7,
      y: 9,
      width: 80,
      height: 78,
    },
    startNS: 870,
    value: 9,
  };
  it('ProcessStructTest01', function () {
    expect(ProcessStruct.draw(ctx, data)).toBeUndefined();
  });
});
