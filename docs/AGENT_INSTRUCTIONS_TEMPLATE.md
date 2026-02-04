Agent Instruction — <Task Title>
Owner: <name>
Status: <unclaimed | claimed by name (YYYY-MM-DD) | done by name (YYYY-MM-DD)>

Summary
<One or two sentences describing the goal and expected outcome.>

Do
1. <Step 1>
2. <Step 2>
3. <Step 3>
4. <Step 4>
5. <Step 5>

Primary files
- <path/to/file.ext>
- <path/to/file.ext>

Acceptance criteria
- <Observable success condition>
- <Observable success condition>
- <Observable success condition>

How to use thepuppeteer (task mutex workflow)
Reference: docs/PUPPETEER.md

1. Claim a task (creates the lock and draft report):
   tools/thepuppeteer/tools/claim_task.sh <task-id> <your-name>
2. Work on the task and update your report file in docs/agent_reports/.
3. Mark the task complete (lock stays to prevent re-claim):
   tools/thepuppeteer/tools/complete_task.sh <task-id> <your-name>
4. Optional validation helpers:
   tools/thepuppeteer/tools/task_status.sh
   tools/thepuppeteer/tools/validate_tasks.sh
   tools/thepuppeteer/tools/generate_reports_index.sh

How to commit a new feature (per docs/RELEASE_STRATEGY.md)

1. Work on a feature branch from develop:
   git checkout develop
   git checkout -b feature/<short-name>
2. Review changes:
   git status
   git diff
3. Update docs when needed:
   - CHANGELOG.md (if user-facing behavior changes)
   - ROADMAP.md (if scope/milestones change)
4. Run relevant tests (pick what’s appropriate for your change).
5. Stage changes:
   git add <files>
6. Use Conventional Commits for release notes:
   - feat: <short description>
   - fix: <short description>
7. Commit:
   git commit -m "feat: <short description>"
8. Push your branch and open a PR to develop:
   git push -u origin feature/<short-name>
