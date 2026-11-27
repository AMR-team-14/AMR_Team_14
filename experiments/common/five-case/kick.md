# 자동 실험 다시 시작하기

자동 루프(실험 → 분석 → 다음 실험)는 아래 경우에 멈춘다: 24시간 실험 상한(기본 30회, 저장소 변수 `AUTO_MAX_PER_DAY`),
AI가 `stop`을 낸 경우, 실행 실패. 다시 시작하려면 `kick.txt`를 바꿔 jsm에 푸시한다.

    date > experiments/common/five-case/kick.txt && git add experiments/common/five-case/kick.txt && git commit -m kick && git push origin jsm

분석이 돌고(새 결과가 없어도 AI 단계 실행), AI가 낸 next.json으로 다음 실험이 시작된다.
모델·effort는 저장소 변수 `CLAUDE_MODEL`(기본 opus), `CLAUDE_EFFORT`(기본 xhigh)로 바꾼다.
