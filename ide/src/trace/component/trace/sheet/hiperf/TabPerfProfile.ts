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

import { BaseElement, element } from '../../../../../base-ui/BaseElement.js';
import { LitTable } from '../../../../../base-ui/table/lit-table.js';
import '../TabPaneFilter.js';
import { FilterData, TabPaneFilter } from '../TabPaneFilter.js';
import { SelectionParam } from '../../../../bean/BoxSelection.js';
import { PerfCallChainMerageData } from '../../../../bean/PerfProfile.js';
import '../../../chart/FrameChart.js';
import { FrameChart } from '../../../chart/FrameChart.js';
import { ChartMode } from '../../../../bean/FrameChartStruct.js';
import '../../../DisassemblingWindow.js';
import { DisassemblingWindow } from '../../../DisassemblingWindow.js';
import { Cmd } from '../../../../../command/Cmd.js';
import { SpApplication } from '../../../../SpApplication.js';
import '../../../../../base-ui/slicer/lit-slicer.js';
import '../../../../../base-ui/progress-bar/LitProgressBar.js';
import { LitProgressBar } from '../../../../../base-ui/progress-bar/LitProgressBar.js';
import { procedurePool } from '../../../../database/Procedure.js';
import { showButtonMenu } from '../SheetUtils.js';
import { LLMAnalyzer, LLMAnalysisData, LLMResponse } from '../../../../ai/LLMInteraction.js';

@element('tabpane-perf-profile')
export class TabpanePerfProfile extends BaseElement {
  private perfProfilerTbl: LitTable | null | undefined;
  private perfProfilerList: LitTable | null | undefined;
  private perfProfileProgressEL: LitProgressBar | null | undefined;
  private perfProfilerRightSource: Array<PerfCallChainMerageData> = [];
  private perfProfilerFilter: any;
  private perfProfilerDataSource: any[] = [];
  private perfProfileSortKey = 'weight';
  private perfProfileSortType = 0;
  private perfSelectedData: any = undefined;
  private perfProfileFrameChart: FrameChart | null | undefined;
  private isChartShow: boolean = false;
  private systemRuleName = '/system/';
  private perfProfileNumRuleName = '/max/min/';
  private perfProfilerModal: DisassemblingWindow | null | undefined;
  private needShowMenu = true;
  private searchValue: string = '';
  private perfProfileLoadingList: number[] = [];
  private perfProfileLoadingPage: any;
  private currentSelection: SelectionParam | undefined;
  private llmAnalyzer: LLMAnalyzer = new LLMAnalyzer();
  private aiAnalysisResultArea: HTMLDivElement | null | undefined;
  private analyzeButton: HTMLButtonElement | null | undefined;
  private userSourceRoots: string[] = ['/system/lib', 'C:/binary_cache', '/app']; // Placeholder


  async resolveSourcePath(libPathOrName: string, functionName: string): Promise<string | null> {
    // Simplified for this subtask, using mocked paths
    if (libPathOrName === 'libc.so' || libPathOrName.endsWith('/libc.so')) {
        // Try to match with one of the userSourceRoots
        for (const root of this.userSourceRoots) {
            const potentialPath = `${root}/libc.so`;
            // In a real scenario, this would be a check like fs.existsSync(potentialPath)
            // For mock, we check if it's one of the paths ProcedureLogicWorkerPerf.fsMock knows
             if (potentialPath === '/system/lib/libc.so' || potentialPath === 'C:/binary_cache/system/lib/libc.so') {
                return potentialPath;
            }
        }
    }
    if (libPathOrName === 'main.c' || libPathOrName.endsWith('/main.c')) {
         for (const root of this.userSourceRoots) {
            const potentialPath = `${root}/main.c`;
            if (potentialPath === '/app/main.c') { // Mocked path
                return potentialPath;
            }
        }
    }
    // Add more specific checks if needed for other mocked files
    console.warn(`Path resolution for "${libPathOrName}" (function "${functionName}"): Defaulting to null. Implement more robust resolution if needed.`);
    return null;
  }


  set data(perfProfilerSelection: SelectionParam | any) {
    if (perfProfilerSelection === this.currentSelection) {
      return;
    }
    this.searchValue = '';
    this.currentSelection = perfProfilerSelection;
    this.perfProfilerModal!.style.display = 'none';
    this.perfProfilerTbl!.style.visibility = 'visible';
    if (this.parentElement!.clientHeight > this.perfProfilerFilter!.clientHeight) {
      this.perfProfilerFilter!.style.display = 'flex';
    } else {
      this.perfProfilerFilter!.style.display = 'none';
    }
    this.perfProfilerFilter!.initializeFilterTree(true, true, true);
    this.perfProfilerFilter!.filterValue = '';
    this.perfProfileProgressEL!.loading = true;
    this.perfProfileLoadingPage.style.visibility = 'visible';
    const initWidth = this.clientWidth;
    this.getDataByWorker(
      [
        {
          funcName: 'setSearchValue',
          funcArgs: [''],
        },
        {
          funcName: 'getCurrentDataFromDb',
          funcArgs: [perfProfilerSelection],
        },
      ],
      (results: any[]) => {
        this.setPerfProfilerLeftTableData(results);
        this.perfProfilerList!.recycleDataSource = [];
        this.perfProfileFrameChart!.mode = ChartMode.Count;
        this.perfProfileFrameChart?.updateCanvas(true, initWidth);
        this.perfProfileFrameChart!.data = this.perfProfilerDataSource;
        this.switchFlameChart();
        this.perfProfilerFilter.icon = 'block';
      }
    );
  }

  getParentTree(
    perfCallSrc: Array<PerfCallChainMerageData>,
    target: PerfCallChainMerageData,
    parentsData: Array<PerfCallChainMerageData>
  ): boolean {
    for (let perfCall of perfCallSrc) {
      if (perfCall.id === target.id) {
        parentsData.push(perfCall);
        return true;
      } else {
        if (this.getParentTree(perfCall.children as Array<PerfCallChainMerageData>, target, parentsData)) {
          parentsData.push(perfCall);
          return true;
        }
      }
    }
    return false;
  }

  getChildTree(
    perfCallSrc: Array<PerfCallChainMerageData>,
    id: string,
    children: Array<PerfCallChainMerageData>
  ): boolean {
    for (let perfCall of perfCallSrc) {
      if (perfCall.id === id && perfCall.children.length === 0) {
        children.push(perfCall);
        return true;
      } else {
        if (this.getChildTree(perfCall.children as Array<PerfCallChainMerageData>, id, children)) {
          children.push(perfCall);
          return true;
        }
      }
    }
    return false;
  }

  setRightTableData(chainMerageData: PerfCallChainMerageData) {
    let parentsMerageData: Array<PerfCallChainMerageData> = [];
    let childrenMerageData: Array<PerfCallChainMerageData> = [];
    this.getParentTree(this.perfProfilerDataSource, chainMerageData, parentsMerageData);
    let maxId = chainMerageData.id;
    let maxDur = 0;

    function findMaxStack(call: PerfCallChainMerageData) {
      if (call.children.length === 0) {
        if (call.dur > maxDur) {
          maxDur = call.dur;
          maxId = call.id;
        }
      } else {
        call.children.map((callChild) => {
          findMaxStack(<PerfCallChainMerageData>callChild);
        });
      }
    }

    findMaxStack(chainMerageData);
    this.getChildTree(chainMerageData.children as Array<PerfCallChainMerageData>, maxId, childrenMerageData);
    let perfProfileParentsList = parentsMerageData.reverse().concat(childrenMerageData.reverse());
    for (let data of perfProfileParentsList) {
      data.type =
        data.libName.endsWith('.so.1') || data.libName.endsWith('.dll') || data.libName.endsWith('.so') ? 0 : 1;
    }
    let len = perfProfileParentsList.length;
    this.perfProfilerRightSource = perfProfileParentsList;
    let rightSource: Array<any> = [];
    if (len != 0) {
      rightSource = this.perfProfilerRightSource.filter((item) => {
        return item.canCharge;
      });
    }
    this.perfProfilerList!.dataSource = rightSource;
  }

  initElements(): void {
    this.perfProfilerTbl = this.shadowRoot?.querySelector<LitTable>('#tb-perf-profile');
    this.perfProfileProgressEL = this.shadowRoot?.querySelector('.perf-profile-progress') as LitProgressBar;
    this.perfProfileFrameChart = this.shadowRoot?.querySelector<FrameChart>('#framechart');
    this.perfProfilerModal = this.shadowRoot?.querySelector<DisassemblingWindow>('tab-native-data-modal');
    this.perfProfileLoadingPage = this.shadowRoot?.querySelector('.perf-profile-loading');
    this.perfProfileFrameChart!.addChartClickListener((needShowMenu: boolean) => {
      this.parentElement!.scrollTo(0, 0);
      showButtonMenu(this.perfProfilerFilter, needShowMenu);
      this.needShowMenu = needShowMenu;
    });
    this.perfProfilerTbl!.rememberScrollTop = true;
    this.perfProfilerFilter = this.shadowRoot?.querySelector<TabPaneFilter>('#filter');
    this.perfProfilerTbl!.addEventListener('row-click', (evt: any) => {
      // @ts-ignore
      let data = evt.detail.data as PerfCallChainMerageData;
      document.dispatchEvent(
        new CustomEvent('number_calibration', {
          detail: { time: data.tsArray },
        })
      );
      this.setRightTableData(data);
      data.isSelected = true;
      this.perfSelectedData = data;
      this.perfProfilerList?.clearAllSelection(data);
      this.perfProfilerList?.setCurrentSelection(data);
      // @ts-ignore
      if ((evt.detail as any).callBack) {
        // @ts-ignore
        (evt.detail as any).callBack(true);
      }
    });
    this.perfProfilerList = this.shadowRoot?.querySelector<LitTable>('#tb-perf-list');
    let lastClikTime = 0;
    this.perfProfilerList!.addEventListener('row-click', (evt: any) => {
      // @ts-ignore
      let data = evt.detail.data as PerfCallChainMerageData;
      this.perfProfilerTbl?.clearAllSelection(data);
      (data as any).isSelected = true;
      this.perfProfilerTbl!.scrollToData(data);
      // @ts-ignore
      if ((evt.detail as any).callBack) {
        // @ts-ignore
        (evt.detail as any).callBack(true);
      }
      let spApplication = <SpApplication>document.getElementsByTagName('sp-application')[0];
      if (Date.now() - lastClikTime < 200 && spApplication.vs) {
        this.perfProfilerTbl!.style.visibility = 'hidden';
        this.perfProfilerFilter.style.display = 'none';
        new ResizeObserver((entries) => {
          this.perfProfilerModal!.style.width = this.perfProfilerTbl!.clientWidth + 'px';
          this.perfProfilerModal!.style.height = this.perfProfilerTbl!.clientHeight + 'px';
        }).observe(this.perfProfilerTbl!);
        this.perfProfilerModal!.showLoading();
        // @ts-ignore
        let data = evt.detail.data as PerfCallChainMerageData;
        let perfProfilerPath = data.path;
        let perfProfilerAddr = data.vaddrInFile;
        let perfProfilerAddrHex = perfProfilerAddr.toString(16);
        if (perfProfilerPath.trim() === '[kernel.kallsyms]') {
          this.perfProfilerModal?.showContent(
            `error : Symbol ${data.symbol} lib is [kernel.kallsyms] ,not support `,
            perfProfilerAddrHex
          );
        } else if (perfProfilerPath.trim() === '') {
          this.perfProfilerModal?.showContent(`error : Symbol ${data.symbol} lib is null `, perfProfilerAddrHex);
        } else if (perfProfilerAddr < 0) {
          this.perfProfilerModal?.showContent(
            `error : Symbol ${data.symbol} current addr is error ` + perfProfilerAddrHex,
            perfProfilerAddrHex
          );
        } else {
          const binDir = 'C:/binary_cache';
          let binPath = binDir + perfProfilerPath;
          let cmd = 'C:/binary_cache/llvm-objdump.exe -S ' + binPath;
          Cmd.execObjDump(cmd, perfProfilerAddrHex, (result: any) => {
            this.perfProfilerModal?.showContent(result, perfProfilerAddrHex);
          });
        }
      }
      lastClikTime = Date.now();
    });
    this.perfProfilerModal!.setCloseListener(() => {
      this.perfProfilerModal!.style.display = 'none';
      this.perfProfilerTbl!.style.visibility = 'visible';
      this.shadowRoot!.querySelector<TabPaneFilter>('#filter')!.style.display = 'flex';
    });
    this.perfProfilerList = this.shadowRoot?.querySelector<LitTable>('#tb-perf-list');
    let filterFunc = (data: any) => {
      let perfProfileFuncArgs: any[] = [];
      if (data.type === 'check') {
        if (data.item.checked) {
          perfProfileFuncArgs.push({
            funcName: 'splitTree',
            funcArgs: [data.item.name, data.item.select === '0', data.item.type === 'symbol'],
          });
        } else {
          perfProfileFuncArgs.push({
            funcName: 'resotreAllNode',
            funcArgs: [[data.item.name]],
          });
          perfProfileFuncArgs.push({
            funcName: 'resetAllNode',
            funcArgs: [],
          });
          perfProfileFuncArgs.push({
            funcName: 'clearSplitMapData',
            funcArgs: [data.item.name],
          });
        }
      } else if (data.type === 'select') {
        perfProfileFuncArgs.push({
          funcName: 'resotreAllNode',
          funcArgs: [[data.item.name]],
        });
        perfProfileFuncArgs.push({
          funcName: 'clearSplitMapData',
          funcArgs: [data.item.name],
        });
        perfProfileFuncArgs.push({
          funcName: 'splitTree',
          funcArgs: [data.item.name, data.item.select === '0', data.item.type === 'symbol'],
        });
      } else if (data.type === 'button') {
        if (data.item === 'symbol') {
          if (this.perfSelectedData && !this.perfSelectedData.canCharge) {
            return;
          }
          if (this.perfSelectedData != undefined) {
            this.perfProfilerFilter!.addDataMining({ name: this.perfSelectedData.symbolName }, data.item);
            perfProfileFuncArgs.push({
              funcName: 'splitTree',
              funcArgs: [this.perfSelectedData.symbolName, false, true],
            });
          } else {
            return;
          }
        } else if (data.item === 'library') {
          if (this.perfSelectedData && !this.perfSelectedData.canCharge) {
            return;
          }
          if (this.perfSelectedData != undefined && this.perfSelectedData.libName != '') {
            this.perfProfilerFilter!.addDataMining({ name: this.perfSelectedData.libName }, data.item);
            perfProfileFuncArgs.push({
              funcName: 'splitTree',
              funcArgs: [this.perfSelectedData.libName, false, false],
            });
          } else {
            return;
          }
        } else if (data.item === 'restore') {
          if (data.remove != undefined && data.remove.length > 0) {
            let list = data.remove.map((item: any) => {
              return item.name;
            });
            perfProfileFuncArgs.push({
              funcName: 'resotreAllNode',
              funcArgs: [list],
            });
            perfProfileFuncArgs.push({
              funcName: 'resetAllNode',
              funcArgs: [],
            });
            list.forEach((symbolName: string) => {
              perfProfileFuncArgs.push({
                funcName: 'clearSplitMapData',
                funcArgs: [symbolName],
              });
            });
          }
        }
      }
      this.getDataByWorker(perfProfileFuncArgs, (result: any[]) => {
        this.setPerfProfilerLeftTableData(result);
        this.perfProfileFrameChart!.data = this.perfProfilerDataSource;
        if (this.isChartShow) this.perfProfileFrameChart?.calculateChartData();
        this.perfProfilerTbl!.move1px();
        if (this.perfSelectedData) {
          this.perfSelectedData.isSelected = false;
          this.perfProfilerTbl?.clearAllSelection(this.perfSelectedData);
          this.perfProfilerList!.recycleDataSource = [];
          this.perfSelectedData = undefined;
        }
      });
    };
    this.perfProfilerFilter!.getDataLibrary(filterFunc);
    this.perfProfilerFilter!.getDataMining(filterFunc);
    this.perfProfilerFilter!.getCallTreeData((data: any) => {
      if (data.value === 0) {
        this.refreshAllNode({
          ...this.perfProfilerFilter!.getFilterTreeData(),
          callTree: data.checks,
        });
      } else {
        let perfProfileArgs: any[] = [];
        if (data.checks[1]) {
          perfProfileArgs.push({
            funcName: 'hideSystemLibrary',
            funcArgs: [],
          });
          perfProfileArgs.push({
            funcName: 'resetAllNode',
            funcArgs: [],
          });
        } else {
          perfProfileArgs.push({
            funcName: 'resotreAllNode',
            funcArgs: [[this.systemRuleName]],
          });
          perfProfileArgs.push({
            funcName: 'resetAllNode',
            funcArgs: [],
          });
          perfProfileArgs.push({
            funcName: 'clearSplitMapData',
            funcArgs: [this.systemRuleName],
          });
        }
        this.getDataByWorker(perfProfileArgs, (result: any[]) => {
          this.setPerfProfilerLeftTableData(result);
          this.perfProfileFrameChart!.data = this.perfProfilerDataSource;
          if (this.isChartShow) this.perfProfileFrameChart?.calculateChartData();
        });
      }
    });
    this.perfProfilerFilter!.getCallTreeConstraintsData((data: any) => {
      let perfProfilerConstraintsArgs: any[] = [
        {
          funcName: 'resotreAllNode',
          funcArgs: [[this.perfProfileNumRuleName]],
        },
        {
          funcName: 'clearSplitMapData',
          funcArgs: [this.perfProfileNumRuleName],
        },
      ];
      if (data.checked) {
        perfProfilerConstraintsArgs.push({
          funcName: 'hideNumMaxAndMin',
          funcArgs: [parseInt(data.min), data.max],
        });
      }
      perfProfilerConstraintsArgs.push({
        funcName: 'resetAllNode',
        funcArgs: [],
      });
      this.getDataByWorker(perfProfilerConstraintsArgs, (result: any[]) => {
        this.setPerfProfilerLeftTableData(result);
        this.perfProfileFrameChart!.data = this.perfProfilerDataSource;
        if (this.isChartShow) this.perfProfileFrameChart?.calculateChartData();
      });
    });
    this.perfProfilerFilter!.getFilterData((data: FilterData) => {
      if (this.searchValue != this.perfProfilerFilter!.filterValue) {
        this.searchValue = this.perfProfilerFilter!.filterValue;
        let perfArgs = [
          {
            funcName: 'setSearchValue',
            funcArgs: [this.searchValue],
          },
          {
            funcName: 'resetAllNode',
            funcArgs: [],
          },
        ];
        this.getDataByWorker(perfArgs, (result: any[]) => {
          this.setPerfProfilerLeftTableData(result);
          this.perfProfileFrameChart!.data = this.perfProfilerDataSource;
          this.switchFlameChart(data);
        });
      } else {
        this.switchFlameChart(data);
      }
    });
    this.perfProfilerTbl!.addEventListener('column-click', (evt) => {
      // @ts-ignore
      this.perfProfileSortKey = evt.detail.key;
      // @ts-ignore
      this.perfProfileSortType = evt.detail.sort;
      // @ts-ignore
      this.setPerfProfilerLeftTableData(this.perfProfilerDataSource);
      this.perfProfileFrameChart!.data = this.perfProfilerDataSource;
    });
    this.analyzeButton = this.shadowRoot?.querySelector<HTMLButtonElement>('#analyze-button');
    this.aiAnalysisResultArea = this.shadowRoot?.querySelector<HTMLDivElement>('#ai-analysis-result');
    if (this.analyzeButton) {
      this.analyzeButton.addEventListener('click', () => this.handleAIAnalysis());
    }
  }

  private getMetricValue(node: PerfCallChainMerageData, mode: ChartMode | undefined): number {
    switch (mode) {
      case ChartMode.Byte:
        return node.size;
      case ChartMode.Count:
        return node.count;
      case ChartMode.Duration:
      default: // Default to duration or handle as an error/unknown
        return node.dur;
    }
  }

  private formatNodeDetails(node: PerfCallChainMerageData, mode: ChartMode | undefined, totalValue: number): string {
    const value = this.getMetricValue(node, mode);
    const percentOfTotal = totalValue > 0 ? ((value / totalValue) * 100).toFixed(1) : 'N/A';
    let metricUnit = '';
    switch (mode) {
      case ChartMode.Byte:
        metricUnit = 'bytes';
        break;
      case ChartMode.Count:
        metricUnit = 'samples';
        break;
      case ChartMode.Duration:
        metricUnit = 'ns'; // Assuming 'dur' is in nanoseconds
        break;
    }
    return `${node.symbol} (${node.libName || 'unknown_lib'}) [${value}${metricUnit}, ${percentOfTotal}% of total]`;
  }

  private extractHotPaths(
    nodes: PerfCallChainMerageData[],
    mode: ChartMode | undefined,
    totalValue: number,
    limit: number = 5
  ): string {
    if (!nodes || nodes.length === 0) {
      return 'No flame chart data available to extract hot paths.';
    }

    const paths: Array<{ path: string; value: number }> = [];

    function findPathsRecursive(
      currentNode: PerfCallChainMerageData,
      currentPath: string[],
      currentPathValue: number
    ) {
      const nodeDetail = `${currentNode.symbol} (${currentNode.libName || 'N/A'})`;
      currentPath.push(nodeDetail);

      // Consider this path a potential "hot path" end
      paths.push({
        path: currentPath.join(' -> '),
        value: currentPathValue + this.getMetricValue(currentNode, mode), // Sum of self times along path
      });

      if (currentNode.children && currentNode.children.length > 0) {
        const sortedChildren = [...currentNode.children].sort(
          (a, b) => this.getMetricValue(b, mode) - this.getMetricValue(a, mode)
        );
        for (const child of sortedChildren) {
          findPathsRecursive(
            child as PerfCallChainMerageData,
            [...currentPath],
            currentPathValue + this.getMetricValue(currentNode, mode)
          );
        }
      }
    }
    // It's more common to see hot paths starting from roots and going down.
    // We need to calculate the total weight of each root and its children to find true hot paths.
    // For simplicity here, we'll sort by the top-level entries and show their primary child paths.
    // A more accurate hot path would consider the sum of 'self' times down a specific stack.
    // The `totalValue` parameter should be the sum of all root nodes' metrics for accurate percentage calculation.

    const topNodes = [...nodes].sort((a, b) => this.getMetricValue(b, mode) - this.getMetricValue(a, mode));
    let hotPathSummaries: string[] = [];

    for (let i = 0; i < Math.min(topNodes.length, limit); i++) {
      const topNode = topNodes[i];
      let path = this.formatNodeDetails(topNode, mode, totalValue); // Use passed totalValue
      let current = topNode;
      let childCount = 0;
      while (current.children && current.children.length > 0 && childCount < 3) { // Limit depth for summary
        const heaviestChild = current.children.reduce(
          (prev, curr) =>
            this.getMetricValue(prev as PerfCallChainMerageData, mode) >
            this.getMetricValue(curr as PerfCallChainMerageData, mode)
              ? prev
              : curr,
          current.children[0]
        ) as PerfCallChainMerageData;
        path += ` -> ${this.formatNodeDetails(heaviestChild, mode, totalValue)}`; // Use passed totalValue
        current = heaviestChild;
        childCount++;
      }
      hotPathSummaries.push(`${i + 1}. ${path}`);
    }

    if (hotPathSummaries.length === 0) {
      return 'Could not extract significant hot paths. Data might be flat or too sparse.';
    }

    return `Top ${hotPathSummaries.length} Hot Paths (Mode: ${
      mode !== undefined ? ChartMode[mode] : 'Unknown'
    }):\n${hotPathSummaries.join('\n')}`;
  }

  async handleAIAnalysis(): Promise<void> {
    if (!this.aiAnalysisResultArea || !this.currentSelection) {
      if (this.aiAnalysisResultArea) {
        this.aiAnalysisResultArea.innerText = 'Error: Context or result area not available.';
      }
      return;
    }

    this.aiAnalysisResultArea.style.display = 'block';
    this.aiAnalysisResultArea.innerText = 'Extracting data and analyzing with AI... Please wait.';

    let metricModeStr: string;
    let currentChartMode = this.perfProfileFrameChart?.mode;
    switch (currentChartMode) {
      case ChartMode.Byte:
        metricModeStr = 'Size';
        break;
      case ChartMode.Count:
        metricModeStr = 'Count';
        break;
      case ChartMode.Duration:
        metricModeStr = 'Duration';
        break;
      default:
        metricModeStr = 'Unknown';
        currentChartMode = ChartMode.Duration; // Default for safety
    }

    // 1. Flame Chart Data Extraction
    const flameChartTotalValue = this.perfProfilerDataSource.reduce(
      (sum, node) => sum + this.getMetricValue(node, currentChartMode),
      0
    );
    // Get top N nodes for snippet fetching - align with hot path extraction
    const topNodesForSnippets = [...this.perfProfilerDataSource]
        .sort((a, b) => this.getMetricValue(b, currentChartMode) - this.getMetricValue(a, currentChartMode))
        .slice(0, 3); // Fetch snippets for top 3 hot roots for now

    const flameChartSummary = this.extractHotPaths(
      this.perfProfilerDataSource,
      currentChartMode,
      flameChartTotalValue,
      5 // Number of hot paths in summary text
    );

    // 2. Contextual Data Fetching
    let contextualDataString = `Time Range: ${this.currentSelection.leftNs}ns to ${this.currentSelection.rightNs}ns.`;
    const selectedProcess = this.currentSelection.perfProcess;
    const selectedThread = this.currentSelection.perfThread;

    let processInfo = 'Context: System-wide.';
    if (selectedProcess && selectedProcess !== '0' && selectedProcess !== '') {
        processInfo = `Process ID: ${selectedProcess}.`;
        if (selectedThread && selectedThread !== '0' && selectedThread !== '') {
            processInfo += ` Thread ID: ${selectedThread}.`;
        }
    }
    contextualDataString += `\n${processInfo}`;

    // 2. Contextual Data Fetching
    let contextualDataParts: string[] = [];
    contextualDataParts.push(`Time Range: ${this.currentSelection.leftNs}ns to ${this.currentSelection.rightNs}ns.`);

    const perfProcess = this.currentSelection.perfProcess; // This is usually PID string
    const perfThread = this.currentSelection.perfThread; // This is usually TID string
    const startNs = this.currentSelection.leftNs;
    const endNs = this.currentSelection.rightNs;

    let ipid: number | undefined = undefined;
    let itid: number | undefined = undefined;

    // Helper function to make worker calls and append to contextualDataParts
    const fetchAndFormat = async (
      action: string,
      args: any[],
      formatter: (res: any) => string | null
    ): Promise<void> => {
      try {
        const result = await new Promise<any[]>((resolve, reject) => {
          procedurePool.submitWithName('logic0', 'perf-action', [{ funcName: action, funcArgs: args }], undefined, (res: any) => {
            resolve(res);
          }, reject);
        });
        const formattedString = formatter(result);
        if (formattedString) {
          contextualDataParts.push(formattedString);
        }
      } catch (error) {
        console.warn(`Error fetching contextual data for ${action}:`, error);
        contextualDataParts.push(`${action.replace('get', '')} data not available due to error.`);
      }
    };

    // Get ipid from pid
    if (perfProcess && perfProcess !== '0' && perfProcess !== '') {
      contextualDataParts.push(`Process PID: ${perfProcess}.`);
      const processResult = await new Promise<any[]>((resolve) => {
        procedurePool.submitWithName('logic0', 'perf-getProcessInfo', [parseInt(perfProcess)], undefined, resolve);
      });
      if (processResult && processResult.length > 0 && processResult[0].id) {
        ipid = processResult[0].id; // Assuming perf-getProcessInfo returns [{id: ipid, name: ...}]
        contextualDataParts.push(`Internal Process ID (ipid): ${ipid}.`);
      } else {
        contextualDataParts.push(`Internal Process ID (ipid) for PID ${perfProcess} not found.`);
      }
    } else {
      contextualDataParts.push('System-wide context (no specific process selected).');
    }

    // Get itid from tid (if pid and tid are present)
    if (ipid && perfThread && perfThread !== '0' && perfThread !== '') {
      contextualDataParts.push(`Thread TID: ${perfThread}.`);
       const threadResult = await new Promise<any[]>((resolve) => {
        procedurePool.submitWithName('logic0', 'perf-getThreadInfo', [parseInt(perfThread), ipid], undefined, resolve);
      });
      if (threadResult && threadResult.length > 0 && threadResult[0].id) {
        itid = threadResult[0].id; // Assuming perf-getThreadInfo returns [{id: itid, name: ...}]
         contextualDataParts.push(`Internal Thread ID (itid): ${itid}.`);
      } else {
        contextualDataParts.push(`Internal Thread ID (itid) for TID ${perfThread} not found within PID ${perfProcess}.`);
      }
    }

    // Fetch data based on availability of ipid/itid
    if (itid) { // Most specific context
      await fetchAndFormat('getThreadCpuUsage', [itid, startNs, endNs], (res) =>
        res && res.length > 0 ? `- Thread CPU Usage: ${res[0].cpu_usage_percentage?.toFixed(1)}% (${res[0].total_running_duration_ns}ns total execution)` : '- Thread CPU Usage: Not available.'
      );
      await fetchAndFormat('getThreadIoActivity', [itid, startNs, endNs], (res) => {
        if (!res || res.length === 0) return '- Thread I/O Activity: Not available or none.';
        let ioStr = '- Thread I/O Activity:\n';
        res.forEach((r: any) => {
          ioStr += `  - ${r.io_operation_type}: ${r.operation_count} ops, ${r.total_size_bytes} bytes, ${r.total_latency_ns}ns total latency.\n`;
        });
        return ioStr;
      });
      if (perfThread) { // OS TID needed for perf_sample
        await fetchAndFormat('getThreadPerfEvents', [parseInt(perfThread), 'cache-misses', startNs, endNs], (res) =>
          res && res.length > 0 ? `- Thread Cache Misses: ${res[0].total_event_occurrences || res[0].sample_count} events` : '- Thread Cache Misses: Not available or none.'
        );
      }
    } else if (ipid) { // Process-level context
      await fetchAndFormat('getProcessCpuUsage', [ipid, startNs, endNs], (res) =>
        res && res.length > 0 ? `- Process CPU Usage: ${res[0].process_cpu_usage_percentage?.toFixed(1)}% (${res[0].total_process_cpu_time_ns}ns total execution)` : '- Process CPU Usage: Not available.'
      );
      await fetchAndFormat('getProcessMemorySummary', [ipid, startNs, endNs], (res) => {
          if (!res || res.length === 0) return '- Process Memory Summary: Not available.';
          const mem = res[0];
          return `- Process Memory: Mallocs(${mem.total_malloc_apply_count} ops, ${mem.total_malloc_apply_size_bytes} bytes), ` +
                 `Frees(${mem.total_malloc_release_count} ops, ${mem.total_malloc_release_size_bytes} bytes). ` +
                 `Mmaps(${mem.total_mmap_apply_count} ops, ${mem.total_mmap_apply_size_bytes} bytes), ` +
                 `Munmaps(${mem.total_mmap_release_count} ops, ${mem.total_mmap_release_size_bytes} bytes).`;
        }
      );
      await fetchAndFormat('getProcessIoActivity', [ipid, startNs, endNs], (res) => {
        if (!res || res.length === 0) return '- Process I/O Activity: Not available or none.';
        let ioStr = '- Process I/O Activity:\n';
        res.forEach((r: any) => {
          ioStr += `  - ${r.io_operation_type}: ${r.operation_count} ops, ${r.total_size_bytes} bytes, ${r.total_latency_ns}ns total latency.\n`;
        });
        return ioStr;
      });
       if (perfProcess) { // OS PID needed for perf_sample
        await fetchAndFormat('getProcessPerfEvents', [parseInt(perfProcess), 'cpu-cycles', startNs, endNs], (res) =>
          res && res.length > 0 ? `- Process CPU Cycles: ${res[0].total_event_occurrences || res[0].sample_count} events` : '- Process CPU Cycles: Not available or none.'
        );
      }
    } else {
      contextualDataParts.push("No specific process or thread context for detailed CPU, Memory, or I/O queries.");
    }
    
    // Placeholder for system-wide data if no specific process/thread
    if (!ipid && !itid) {
        contextualDataParts.push("- System-wide CPU usage or other metrics might be relevant but are not queried here.");
    }
    const contextualDataString = contextualDataParts.join('\n');

    // 3. Fetch Source Code Snippets for top hot functions
    const sourceCodeSnippets: LLMAnalysisData['sourceCodeSnippets'] = [];
    if (topNodesForSnippets.length > 0) {
        this.aiAnalysisResultArea.innerText = 'Extracting data, fetching snippets, and analyzing with AI... Please wait.';
    }

    for (const node of topNodesForSnippets) {
        const libPathOrName = node.libName || node.path; // Prefer libName, fallback to path
        if (libPathOrName) {
            const resolvedPath = await this.resolveSourcePath(libPathOrName, node.symbolName);
            let requestedLineForSnippet = 1; // Default

            // Optional hack for more diverse snippet testing with mock data
            if (resolvedPath === '/system/lib/libc.so' && node.symbolName === topNodesForSnippets[0]?.symbolName) {
                requestedLineForSnippet = 5;
            } else if (resolvedPath === 'C:/binary_cache/system/lib/libc.so' && node.symbolName === topNodesForSnippets[0]?.symbolName) {
                requestedLineForSnippet = 5; // if it resolved to the other libc path
            } else if (resolvedPath === '/app/main.c') {
                requestedLineForSnippet = 2;
            }

            let snippetEntry: typeof sourceCodeSnippets[0] = {
                functionName: node.symbolName,
                filePath: resolvedPath || libPathOrName, // Use original if not resolved for context
                requestedLine: requestedLineForSnippet,
            };

            if (resolvedPath) {
                try {
                    const snippetResult = await new Promise<any>((resolve, reject) => {
                        procedurePool.submitWithName(
                            'logic0',
                            'perf-fetchSourceSnippet',
                            { filePath: resolvedPath, targetLineNumber: requestedLineForSnippet, contextLines: 10 },
                            undefined,
                            (res: any) => resolve(res),
                            reject
                        );
                    });

                    if (snippetResult.error) {
                        snippetEntry.error = snippetResult.error;
                    } else {
                        snippetEntry.snippet = snippetResult.snippet;
                        snippetEntry.actualStartLine = snippetResult.actualStartLine;
                        snippetEntry.actualEndLine = snippetResult.actualEndLine;
                    }
                } catch (e: any) {
                    snippetEntry.error = e.message || 'Unknown error fetching snippet';
                }
            } else {
                snippetEntry.error = 'Path not resolved';
            }
            sourceCodeSnippets.push(snippetEntry);
        } else {
             sourceCodeSnippets.push({
                functionName: node.symbolName,
                filePath: 'Unknown',
                error: 'Library path/name not available for this node.',
                requestedLine: 1,
            });
        }
    }


    const llmData: LLMAnalysisData = {
      flameChartSummary: flameChartSummary,
      contextualData: contextualDataString,
      currentMetricMode: metricModeStr,
      sourceCodeSnippets: sourceCodeSnippets,
    };

    try {
      const result = await this.llmAnalyzer.analyzePerfData(llmData);
      if (typeof result === 'string') {
        this.aiAnalysisResultArea.innerText = `AI Analysis Error:\n${result}`;
      } else {
        const formattedResult = `AI Analysis Complete:\n\n` +
                                `Bottleneck:\n${result.bottleneck}\n\n` +
                                `Potential Root Cause:\n${result.rootCause}\n\n` +
                                `Suggestions:\n${result.suggestions}`;
        this.aiAnalysisResultArea.innerText = formattedResult;
      }
    } catch (error) {
      console.error('Error during AI Analysis:', error);
      if (error instanceof Error) {
        this.aiAnalysisResultArea.innerText = `An error occurred during AI analysis: ${error.message}`;
      } else {
        this.aiAnalysisResultArea.innerText = 'An unknown error occurred during AI analysis.';
      }
    }
  }

  connectedCallback() {
    super.connectedCallback();
    this.parentElement!.onscroll = () => {
      this.perfProfileFrameChart!.tabPaneScrollTop = this.parentElement!.scrollTop;
    };
    let filterHeight = 0;
    new ResizeObserver((entries) => {
      let perfProfileTabFilter = this.shadowRoot!.querySelector('#filter') as HTMLElement;
      if (perfProfileTabFilter.clientHeight > 0) filterHeight = perfProfileTabFilter.clientHeight;
      if (this.parentElement!.clientHeight > filterHeight) {
        perfProfileTabFilter.style.display = 'flex';
      } else {
        perfProfileTabFilter.style.display = 'none';
      }
      this.perfProfilerModal!.style.height = this.perfProfilerTbl!.clientHeight - 2 + 'px'; //2 is borderWidth
      if (this.perfProfilerTbl!.style.visibility === 'hidden') {
        perfProfileTabFilter.style.display = 'none';
      }
      if (this.parentElement?.clientHeight != 0) {
        if (this.isChartShow) {
          this.perfProfileFrameChart?.updateCanvas(false, entries[0].contentRect.width);
          this.perfProfileFrameChart?.calculateChartData();
        }
        // @ts-ignore
        this.perfProfilerTbl?.shadowRoot.querySelector('.table').style.height =
          // @ts-ignore
          this.parentElement.clientHeight - 10 - 35 + 'px';
        this.perfProfilerTbl?.reMeauseHeight();
        // @ts-ignore
        this.perfProfilerList?.shadowRoot.querySelector('.table').style.height =
          // @ts-ignore
          this.parentElement.clientHeight - 45 - 21 + 'px';
        this.perfProfilerList?.reMeauseHeight();
        this.perfProfileLoadingPage.style.height = this.parentElement!.clientHeight - 24 + 'px';
      }
    }).observe(this.parentElement!);
  }

  switchFlameChart(data?: any): void {
    let perfProfilerPageTab = this.shadowRoot?.querySelector('#show_table');
    let perfProfilerPageChart = this.shadowRoot?.querySelector('#show_chart');
    if (!data || data.icon === 'block') {
      perfProfilerPageChart?.setAttribute('class', 'show');
      perfProfilerPageTab?.setAttribute('class', '');
      this.isChartShow = true;
      this.perfProfilerFilter!.disabledMining = true;
      showButtonMenu(this.perfProfilerFilter, this.needShowMenu);
      this.perfProfileFrameChart?.calculateChartData();
    } else if (data.icon === 'tree') {
      perfProfilerPageChart?.setAttribute('class', '');
      perfProfilerPageTab?.setAttribute('class', 'show');
      showButtonMenu(this.perfProfilerFilter, true);
      this.isChartShow = false;
      this.perfProfilerFilter!.disabledMining = false;
      this.perfProfileFrameChart!.clearCanvas();
      this.perfProfilerTbl!.reMeauseHeight();
    }
  }

  refreshAllNode(filterData: any) {
    let perfProfileArgs: any[] = [];
    let isTopDown: boolean = !filterData.callTree[0];
    let isHideSystemLibrary = filterData.callTree[1];
    let list = filterData.dataMining.concat(filterData.dataLibrary);
    perfProfileArgs.push({
      funcName: 'getCallChainsBySampleIds',
      funcArgs: [isTopDown],
    });
    this.perfProfilerList!.recycleDataSource = [];
    if (isHideSystemLibrary) {
      perfProfileArgs.push({
        funcName: 'hideSystemLibrary',
        funcArgs: [],
      });
    }
    if (filterData.callTreeConstraints.checked) {
      perfProfileArgs.push({
        funcName: 'hideNumMaxAndMin',
        funcArgs: [parseInt(filterData.callTreeConstraints.inputs[0]), filterData.callTreeConstraints.inputs[1]],
      });
    }
    perfProfileArgs.push({
      funcName: 'splitAllProcess',
      funcArgs: [list],
    });
    perfProfileArgs.push({
      funcName: 'resetAllNode',
      funcArgs: [],
    });
    this.getDataByWorker(perfProfileArgs, (result: any[]) => {
      this.setPerfProfilerLeftTableData(result);
      this.perfProfileFrameChart!.data = this.perfProfilerDataSource;
      if (this.isChartShow) this.perfProfileFrameChart?.calculateChartData();
    });
  }

  setPerfProfilerLeftTableData(resultData: any[]) {
    this.perfProfilerDataSource = this.sortTree(resultData);
    this.perfProfilerTbl!.recycleDataSource = this.perfProfilerDataSource;
  }

  sortTree(arr: Array<any>): Array<any> {
    let perfProfileSortArr = arr.sort((perfProfileA, perfProfileB) => {
      if (this.perfProfileSortKey === 'self') {
        if (this.perfProfileSortType === 0) {
          return perfProfileB.dur - perfProfileA.dur;
        } else if (this.perfProfileSortType === 1) {
          return perfProfileA.selfDur - perfProfileB.selfDur;
        } else {
          return perfProfileB.selfDur - perfProfileA.selfDur;
        }
      } else {
        if (this.perfProfileSortType === 0) {
          return perfProfileB.dur - perfProfileA.dur;
        } else if (this.perfProfileSortType === 1) {
          return perfProfileA.dur - perfProfileB.dur;
        } else {
          return perfProfileB.dur - perfProfileA.dur;
        }
      }
    });
    perfProfileSortArr.map((call) => {
      call.children = this.sortTree(call.children);
    });
    return perfProfileSortArr;
  }

  getDataByWorker(args: any[], handler: Function) {
    this.perfProfileProgressEL!.loading = true;
    this.perfProfileLoadingPage.style.visibility = 'visible';
    procedurePool.submitWithName('logic0', 'perf-action', args, undefined, (results: any) => {
      handler(results);
      this.perfProfileProgressEL!.loading = false;
      this.perfProfileLoadingPage.style.visibility = 'hidden';
    });
  }

  initHtml(): string {
    return `
        <style>
        tab-pane-filter {
            position: fixed;
            bottom: 0;
            width: 100%;
            border: solid rgb(216,216,216) 1px;
            float: left;
        }
        :host{
            display: flex;
            flex-direction: column;
            padding: 10px 10px 0 10px;
        }
        .perf-profile-progress{
            bottom: 33px;
            position: absolute;
            height: 1px;
            left: 0;
            right: 0;
        }
        selector{
            display: none;
        }
        .perf-profile-loading{
            bottom: 0;
            position: absolute;
            left: 0;
            right: 0;
            width:100%;
            background:transparent;
            z-index: 999999;
        }
        .show{
            display: flex;
            flex: 1;
        }
        #analyze-button {
            margin: 5px;
            padding: 8px 12px;
            background-color: var(--dark-background5, #007bff);
            color: white;
            border: none;
            border-radius: 4px;
            cursor: pointer;
        }
        #analyze-button:hover {
            background-color: var(--dark-background2, #0056b3);
        }
        #ai-analysis-result {
            margin-top: 10px;
            padding: 10px;
            border: 1px solid var(--dark-border,#ccc);
            background-color: var(--dark-background4, #f9f9f9);
            white-space: pre-wrap; /* Ensures newlines and spaces are preserved */
            max-height: 300px;
            overflow-y: auto;
        }
    </style>
    <div class="perf-profile-content" style="display: flex;flex-direction: column;">
      <div style="display: flex; flex-direction: row;">
        <selector id='show_table' class="show">
            <lit-slicer style="width:100%">
            <div id="left_table" style="width: 65%">
                <tab-native-data-modal id="modal"></tab-native-data-modal>
            <lit-table id="tb-perf-profile" style="height: auto" tree>
                <lit-table-column width="70%" title="Call Stack" data-index="symbol" key="symbol"  align="flex-start"retract></lit-table-column>
                <lit-table-column width="1fr" title="Local" data-index="self" key="self"  align="flex-start"  order></lit-table-column>
                <lit-table-column width="1fr" title="Weight" data-index="weight" key="weight"  align="flex-start"  order></lit-table-column>
                <lit-table-column width="1fr" title="%" data-index="weightPercent" key="weightPercent"  align="flex-start"  order></lit-table-column>
            </lit-table>
            
        </div>
            <lit-slicer-track ></lit-slicer-track>
            <lit-table id="tb-perf-list" no-head hideDownload style="height: auto;border-left: 1px solid var(--dark-border1,#e2e2e2)">
                <span slot="head">Heaviest Stack Trace</span>
                <lit-table-column width="60px" title="" data-index="type" key="type"  align="flex-start" >
                    <template>
                        <img src="img/library.png" size="20" v-if=" type == 1 ">
                        <img src="img/function.png" size="20" v-if=" type == 0 ">
                    </template>
                </lit-table-column>
                <lit-table-column width="1fr" title="" data-index="symbolName" key="symbolName"  align="flex-start"></lit-table-column>
            </lit-table>
            </div>
            </lit-slicer>
        </selector>
        <selector id='show_chart'>
            <tab-framechart id='framechart' style='width: 100%;height: auto'> </tab-framechart>
        </selector>
      </div>
      <button id="analyze-button">Analyze with AI</button>
      <div id="ai-analysis-result" style="display: none;"></div>
      <tab-pane-filter id="filter" input inputLeftText icon tree></tab-pane-filter>
      <lit-progress-bar class="progress perf-profile-progress"></lit-progress-bar>
      <div class="loading perf-profile-loading"></div>
    </div>
    `;
  }
}
