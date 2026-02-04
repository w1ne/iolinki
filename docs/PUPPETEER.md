# thepuppeteer Workflow (Task Mutex)

This repo uses the **thepuppeteer** submodule to coordinate agent work via a file‑based mutex.

## Bootstrap in a New Repo

**Fast path (after adding the submodule):**

```bash
tools/thepuppeteer/tools/bootstrap_repo.sh
```

1. Add the submodule:
   ```bash
   git submodule add git@github.com:w1ne/thepuppeteer.git tools/thepuppeteer
   git submodule update --init --recursive
   ```

2. Copy task templates **into this repo** (each repo owns its own tasks and reports):
   ```bash
   cp tools/thepuppeteer/docs/AGENT_TASKS.md docs/AGENT_TASKS.md
   cp tools/thepuppeteer/docs/AGENT_REPORTS.md docs/AGENT_REPORTS.md
   mkdir -p docs/claims
   cp tools/thepuppeteer/docs/claims/README.md docs/claims/README.md
   ```

   Or run:
   ```bash
   tools/thepuppeteer/tools/bootstrap_repo.sh --force
   ```

3. Add lock ignore (do not commit lock dirs):
   ```bash
   printf "task-*.lock/\\n" > docs/claims/.gitignore
   ```

4. Use the scripts from the submodule path:
   ```bash
   tools/thepuppeteer/tools/claim_task.sh <task-id> <name>
   tools/thepuppeteer/tools/complete_task.sh <task-id> <name>
   tools/thepuppeteer/tools/task_status.sh
   tools/thepuppeteer/tools/validate_tasks.sh
   ```

## Notes

- **Do not edit** files inside `tools/thepuppeteer` for project‑specific tasks.  
  Your repo’s task list and reports live in `docs/AGENT_TASKS.md` and `docs/AGENT_REPORTS.md`.
- Update the submodule when you want new workflow tooling:
  ```bash
  git submodule update --remote --merge
  ```
