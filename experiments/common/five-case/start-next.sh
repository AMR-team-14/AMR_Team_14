#!/usr/bin/env bash
# Start the experiment the analysis proposed (next.json), within code limits. Called by jsm-analyze.yml.
#   start-next.sh <owner/repo> <analysis run id>
# Needs JSM_PUSH_TOKEN in the checkout (HAS_PAT=true): a push made with it starts jsm-five-case; the default token
# cannot start workflows. Stops when the last 24 hours already had MAX_PER_DAY experiment runs.
set -euo pipefail
REPO="$1"; RUN="$2"
DIR=experiments/common/five-case
cp next.json "$DIR/reports/next-run$RUN.json"
if [ "${HAS_PAT:-false}" != "true" ]; then echo "JSM_PUSH_TOKEN 없음: 다음 실험은 제안만 하고 시작하지 않음"; exit 0; fi
SINCE=$(date -u -d '24 hours ago' +%Y-%m-%dT%H:%M:%SZ)
N=$(gh api "repos/$REPO/actions/workflows/jsm-five-case.yml/runs?branch=jsm&created=>=$SINCE&per_page=1" -q '.total_count')
if [ "$N" -ge "${MAX_PER_DAY:-30}" ]; then echo "최근 24시간 실험 $N회, 상한 ${MAX_PER_DAY:-30}회: 시작하지 않음"; exit 0; fi
git config user.name "github-actions[bot]"
git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
for i in 1 2 3 4; do
  git fetch --depth=1 origin jsm
  git checkout -q -f -B auto-next FETCH_HEAD
  set +e; node "$DIR/next-plan.cjs" next.json; RC=$?; set -e
  if [ "$RC" = 3 ]; then exit 0; fi
  if [ "$RC" != 0 ]; then exit 1; fi
  cp next.json "$DIR/reports/next-run$RUN.json"
  git add -f "$DIR/jobs" "$DIR/overnight-request.txt" experiments/case*/candidates "$DIR/reports/next-run$RUN.json"
  B=$(cut -d' ' -f1 "$DIR/overnight-request.txt")
  if git commit -q -m "Auto experiment $B (proposed by analysis run $RUN)" && git push origin HEAD:jsm; then exit 0; fi
  sleep $((i * 5))
done
exit 1
