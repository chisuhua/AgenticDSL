# AgenticDSL Fixes - Learnings

## 2026-02-27T08:37:44.039Z - Session Start
- Plan: agenticdsl-fixes
- Session ID: ses_36802c4bcffea1IcWIFFkKaXws
- Status: Resuming existing work (0/21 tasks completed)

## Key Findings from Previous Research
1. **Model path bug**: `engine.cpp` lines 36-43 - `fs::path(config_path).parent_path()` returns empty path, falls back to `.` (current working directory)
2. **Scheduler bug**: `topo_scheduler.cpp` lines 594-598 - `execute_single_branch()` filters successors by branch path prefix, preventing cross-graph edges
3. **Workflow format**: Split graphs are valid per v3.9 spec, nodes connect via absolute paths in `next` field
4. **Tool system**: `custom_db_query` mock exists in example main.cpp, 3 default tools in registry
5. **Test infrastructure**: Catch2 v3 exists, test-to-source ratio 0.37:1, no coverage/CI

## Guardrails (from Metis review)
- MUST NOT modify files outside `engine.cpp` and `topo_scheduler.cpp`
- MUST NOT change NodePath format or add CLI arguments
- MUST NOT fix both issues in single commit
- MUST write failing tests BEFORE implementing fixes
- MUST verify ALL existing tests pass after each change
- MUST use `fs::weakly_canonical` for path resolution (handles symlinks)

## Plan Structure
- **Wave 1** (Parallel): Tasks 1-4 (Validation + TDD setup)
- **Wave 2** (Parallel after Wave 1): Tasks 5-9 (Implementation fixes)
- **Final Wave** (Parallel): Tasks F1-F2 (Review agents)

## Critical Acceptance Criteria
1. Model loads from project root without "Failed to load model" error
2. Model loads from build directory without "Failed to load model" error
3. All tests pass: `ctest --output-on-failure` shows 0 failures
4. Cross-graph test passes: `./tests/test_scheduler "[stage1][scheduler]"` passes

## Task 1: Validation Evidence Captured

### Date: 2026-02-27

### Findings:

1. **Model Path Bug Confirmed** (engine.cpp lines 36-43)
   - Error: Looking for model at `/home/dev/workspace/AgenticDSL/build/./models/gongjy/MiniMind2-gguf/Q4-MiniMind2-Small.gguf`
   - Actual location: `/home/dev/workspace/AgenticDSL/models/gongjy/MiniMind2-gguf/Q4-MiniMind2-Small.gguf`
   - Root cause: Path resolution relative to build directory instead of project root

2. **Model File Exists**
   - File: `/home/dev/workspace/AgenticDSL/models/gongjy/MiniMind2-gguf/Q4-MiniMind2-Small.gguf`
   - Size: 17506368 bytes
   - Status: ✓ Present and readable

3. **Test Build Failure**
   - Error: `agenticdsl/core/engine.h: No such file or directory`
   - Actual path: `src/core/engine.h`
   - Include path mapping is broken in CMake or test files

4. **custom_db_query Usage**
   - Found at line 30 in workflow.agent.md
   - Usage: Tool call node `/main/query` with arguments table="users", filter="active=true"
   - Output key: db_result

### Evidence Files Created:
- task-1-error-output.txt: Full agent_basic error output
- task-1-exit-code.txt: Exit code (1 = failure)
- task-1-model-exists.txt: Model file verification
- task-1-test-output.txt: Test build failure details
