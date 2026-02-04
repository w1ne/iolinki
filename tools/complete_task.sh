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

if [ ! -d "${LOCK_DIR}" ]; then
  echo "Task ${TASK_ID} is not claimed. No lock directory found."
  exit 1
fi

if [ -f "${LOCK_FILE}" ]; then
  OWNER="$(awk -F= '/claimed_by=/{print $2}' "${LOCK_FILE}")"
  if [ -n "${OWNER}" ] && [ "${OWNER}" != "${NAME}" ]; then
    echo "Task ${TASK_ID} is claimed by ${OWNER}. Only the owner should complete it."
    exit 1
  fi
fi

awk -v id="${TASK_ID}" -v name="${NAME}" -v date="${DATE}" '
  $0 ~ "^## Task " id ":" {
    print;
    if (getline) {
      if ($0 ~ "^Status:") {
        print "Status: done by " name " (" date ")";
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
  echo "Status: done"
  echo "Summary:"
  echo "- Completed by ${NAME} (${DATE})"
  echo "Files:"
  echo "- ..."
  echo "Tests:"
  echo "- ..."
  echo "Follow-ups:"
  echo "- ..."
  echo ""
} >> "${REPORTS_FILE}"

echo "Marked task ${TASK_ID} as done by ${NAME}."
