#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 2 ]; then
  echo "Usage: $0 <task-id> <name>"
  exit 2
fi

TASK_ID="$1"
NAME="$2"
DATE="$(date +%F)"

LOCK_DIR="docs/claims/task-${TASK_ID}.lock"
LOCK_FILE="${LOCK_DIR}/claim.txt"

if mkdir "${LOCK_DIR}" 2>/dev/null; then
  printf "claimed_by=%s\nclaimed_on=%s\n" "${NAME}" "${DATE}" > "${LOCK_FILE}"
  awk -v id="${TASK_ID}" -v name="${NAME}" -v date="${DATE}" '
    $0 ~ "^## Task " id ":" {
      print;
      if (getline) {
        if ($0 ~ "^Status:") {
          print "Status: claimed by " name " (" date ")";
        } else {
          print $0;
        }
      }
      next
    }
    {print}
  ' docs/AGENT_TASKS.md > /tmp/AGENT_TASKS.md && mv /tmp/AGENT_TASKS.md docs/AGENT_TASKS.md

  REPORTS_FILE="docs/AGENT_REPORTS.md"
  if [ ! -f "${REPORTS_FILE}" ]; then
    printf "# Agent Reports\n\n" > "${REPORTS_FILE}"
  fi
  TITLE="$(awk -v id="${TASK_ID}" '$0 ~ "^## Task " id ":" {sub(/^## Task [0-9]+: /, "", $0); print; exit}' docs/AGENT_TASKS.md)"
  {
    echo "## Task: ${TASK_ID} - ${TITLE}"
    echo "Status: in progress"
    echo "Summary:"
    echo "- Claimed by ${NAME} (${DATE})"
    echo "Files:"
    echo "- ..."
    echo "Tests:"
    echo "- not run"
    echo "Follow-ups:"
    echo "- ..."
    echo ""
  } >> "${REPORTS_FILE}"

  echo "Claimed task ${TASK_ID} for ${NAME}."
else
  echo "Task ${TASK_ID} already claimed."
  if [ -f "${LOCK_FILE}" ]; then
    cat "${LOCK_FILE}"
  fi
  exit 1
fi
