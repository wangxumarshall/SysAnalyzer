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
import { SpHisysEvent } from '../../../../dist/trace/component/setting/SpHisysEvent.js';

describe('SpHisysEvent Test', () => {
    let spHisysEvent = new SpHisysEvent();
    it('SpHisysEventTest01', function () {
        expect(spHisysEvent.startSamp).toBeFalsy();
    });
    it('SpHisysEventTest02', function () {
        expect(spHisysEvent.process).toBeFalsy();
    });
})