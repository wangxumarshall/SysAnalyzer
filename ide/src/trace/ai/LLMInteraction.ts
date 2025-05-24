// Copyright (C) 2023 Huawei Device Co., Ltd.
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/**
 * Represents the data structure expected for LLM analysis.
 */
export interface LLMAnalysisData {
  /**
   * A summary of the hot paths identified in the flame chart.
   * This can be a string or a more structured object (e.g., an array of hot path nodes).
   */
  flameChartSummary: string | object;

  /**
   * Additional contextual data that might be relevant for performance analysis.
   * Examples include CPU load, memory pressure, I/O activity, or specific system events.
   */
  contextualData: string | object;

  /**
   * The current metric mode of the flame chart (e.g., "Duration", "Count", "Size").
   * This helps the LLM understand the primary dimension of the flame chart data.
   */
  currentMetricMode: 'Duration' | 'Count' | 'Size' | string;

  /**
   * Optional array of source code snippets relevant to the hot paths.
   */
  sourceCodeSnippets?: Array<{
    functionName: string; // The function associated with this snippet
    filePath: string; // The resolved file path for the snippet
    snippet?: string; // The actual code snippet
    error?: string; // Error message if snippet fetching failed
    requestedLine?: number; // The line number that was targeted (if any)
    actualStartLine?: number; // Actual start line of the fetched snippet
    actualEndLine?: number; // Actual end line of the fetched snippet
  }>;
}

/**
 * Represents the expected structure of the LLM's analysis response.
 */
export interface LLMResponse {
  /**
   * Identified performance bottleneck(s).
   */
  bottleneck: string;
  /**
   * Potential root cause(s) for the identified bottleneck(s).
   */
  rootCause: string;
  /**
   * Actionable optimization suggestions.
   */
  suggestions: string;
}

/**
 * The LLMAnalyzer class is responsible for interacting with a Large Language Model (LLM)
 * to analyze performance data, identify bottlenecks, and suggest optimizations.
 */
export class LLMAnalyzer {
  private readonly llmApiEndpoint: string;

  /**
   * Constructs an LLMAnalyzer instance.
   * @param llmApiEndpoint The API endpoint for the LLM service. Defaults to a placeholder.
   */
  constructor(llmApiEndpoint: string = 'https://api.example.com/llm/analyze') {
    this.llmApiEndpoint = llmApiEndpoint;
  }

  /**
   * Analyzes performance data using an LLM.
   * It constructs a prompt, sends it to the LLM (simulated), and returns the LLM's analysis.
   *
   * @param data The performance data to analyze, including flame chart summary and contextual information.
   * @returns A Promise that resolves to a string containing the LLM's analysis, or a structured LLMResponse.
   *          In case of an error, it returns an error message string.
   */
  public async analyzePerfData(data: LLMAnalysisData): Promise<string | LLMResponse> {
    const prompt = this.constructPrompt(data);

    try {
      // Simulate API call
      console.log(`Sending prompt to LLM at ${this.llmApiEndpoint}:`);
      console.log(JSON.stringify({ prompt }, null, 2));

      // In a real scenario, you would use fetch here:
      /*
      const response = await fetch(this.llmApiEndpoint, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          // 'Authorization': 'Bearer YOUR_API_KEY' // If authentication is needed
        },
        body: JSON.stringify({ prompt }),
      });

      if (!response.ok) {
        throw new Error(`LLM API request failed with status ${response.status}: ${response.statusText}`);
      }
      const llmResult: LLMResponse = await response.json();
      return llmResult;
      */

      // Simulate receiving a canned response after a short delay
      await new Promise(resolve => setTimeout(resolve, 500));

      const simulatedResponse: LLMResponse = {
        bottleneck: `High execution time in function "X" and its children, as indicated by the ${data.currentMetricMode} mode.`,
        rootCause: `Function "X" might be performing inefficient computations or experiencing resource contention. 
                    Contextual data (${JSON.stringify(data.contextualData)}) suggests potential high CPU load during its execution.`,
        suggestions: `1. Review the implementation of function "X" for potential algorithmic optimizations.
                      2. If "X" is I/O bound, consider asynchronous operations.
                      3. Profile "X" with more granularity to pinpoint specific lines of code causing delays.
                      4. Investigate the high CPU load indicated by contextual data to see if it's related or a separate issue.`,
      };

      // For now, returning the structured object. Could also be stringified.
      return simulatedResponse;

    } catch (error) {
      console.error('Error during LLM analysis:', error);
      if (error instanceof Error) {
        return `Error analyzing performance data: ${error.message}`;
      }
      return 'An unknown error occurred during LLM analysis.';
    }
  }

  /**
   * Constructs a detailed prompt for the LLM based on the provided performance data.
   *
   * @param data The performance data including flame chart summary, contextual data, and metric mode.
   * @returns A string representing the prompt to be sent to the LLM.
   */
  private constructPrompt(data: LLMAnalysisData): string {
    let flameChartDetails: string;
    if (typeof data.flameChartSummary === 'string') {
      flameChartDetails = data.flameChartSummary;
    } else {
      // Basic stringification for object summary, can be made more sophisticated
      flameChartDetails = JSON.stringify(data.flameChartSummary, null, 2);
    }

    let contextInfo: string;
    if (typeof data.contextualData === 'string') {
      contextInfo = data.contextualData;
    } else {
      contextInfo = JSON.stringify(data.contextualData, null, 2);
    }

    return `
Analyze the following performance data to identify bottlenecks, suggest potential root causes, and provide actionable optimization suggestions.

Current Flame Chart Metric Mode: ${data.currentMetricMode}

Flame Chart Summary (Hot Paths / Key Observations):
---
${flameChartDetails}
---

Additional Contextual Data (e.g., CPU Load, Memory Pressure, System Events, I/O Activity):
---
${contextInfo}
---
${data.sourceCodeSnippets && data.sourceCodeSnippets.length > 0 ? `

Relevant Source Code Snippets:
---
${data.sourceCodeSnippets
  .map(
    (s) => `
Snippet for function "${s.functionName}" from file "${s.filePath}" (around line ${s.requestedLine || 'N/A'}):
Actual lines: ${s.actualStartLine || 'N/A'} to ${s.actualEndLine || 'N/A'}
${s.error ? `Error fetching snippet: ${s.error}` : s.snippet}
`
  )
  .join('\\n---')}
---
` : ''}

Based on all the provided information, including any source code snippets, please:
1.  Identify the primary performance bottleneck(s). Focus on what the flame chart data indicates as most significant given the "${data.currentMetricMode}" metric.
2.  Explain potential root causes for these bottlenecks. **Explicitly state how the contextual data and source code snippets (if available and relevant) support or contradict potential causes.**
3.  Provide clear, actionable optimization suggestions. **If possible, prioritize these suggestions by potential impact or ease of implementation. Refer to specific lines in snippets if applicable.**
4.  (Optional) If the root cause is ambiguous based on the provided data, you may state this and list alternative potential causes with a brief explanation for each.

Structure your response in JSON format with the following keys: "bottleneck", "rootCause", "suggestions".
`;
  }
}

// Example Usage (can be removed or kept for testing)
/*
async function testLLMAnalyzer() {
  const analyzer = new LLMAnalyzer();
  const testData: LLMAnalysisData = {
    flameChartSummary: {
      hotPath1: "main() -> process_data() -> calculate_heavy_stuff() accounts for 60% of Duration",
      hotPath2: "main() -> render_ui() -> draw_complex_graphics() accounts for 25% of Duration"
    },
    contextualData: {
      cpuLoad: "Consistently above 85% during the trace",
      memoryWarnings: "Multiple low memory warnings observed"
    },
    currentMetricMode: "Duration"
  };

  const analysisResult = await analyzer.analyzePerfData(testData);

  if (typeof analysisResult === 'string') {
    console.error("Analysis failed:", analysisResult);
  } else {
    console.log("LLM Analysis Result:");
    console.log("Bottleneck:", analysisResult.bottleneck);
    console.log("Root Cause:", analysisResult.rootCause);
    console.log("Suggestions:", analysisResult.suggestions);
  }
}

// testLLMAnalyzer();
*/
