# 누적 인사이트 (낮을수록 좋음, 평가기 jsim:e2dc473b4509+ev1, digest 2026-11-26T09:17:50Z)

## Case 3
확인된 인사이트
- 공개(11-15) 최저 962.7048 = baseline(fleet 22/D1), 팀 기록 962.7과 동일. 탐색은 못 넘음.
- 별도 시드 최고는 23/D1(2fcf90e69eb4, adjMean 976.73)이나 baseline과 구별 안 됨 [ev:c3.vsbest.00fad1b682e2=indistinct]
- 21/D1 탐색 후보는 최고보다 나쁨 [ev:c3.vsbest.a5c1da947d90=worse]; 22/D2 탐색 후보도 나쁨 [ev:c3.vsbest.f5d9cca6b020=worse]
- dir이 비용을 좌우: D1 vs D4 −386.18 [ev:c3.effect.a6901ec21c7d.55725d4e0e7b=a-better], D2 vs D6 −52 [ev:c3.effect.d1294e427d28.93c5a03a49bc=a-better]. dir 쌍 요약 a-better 93/a-worse 39/indistinct 9
- 대수 쌍 효과는 52/90이 구별 안 됨(a-better 22, a-worse 16)
- 공개 대비 별도 시드 악화: baseline −15.56(공개−별도), 최고 후보 −6.82 → 공개 최저는 과적합 경향
- overnight-1125 탐색(234회) 최저 1024.2547(21/D31), baseline 못 넘음
검증 필요 가설
- 23/D1 이득이 트리 때문인지 대수 때문인지(동시에 다름)
- baseline dir이 전역 최적 근방인지(dir당 국소 탐색 1회뿐)
다음 확인할 것
- 시드 2420001~ 480블록 OCBA(2fcf 151, 3a0a 110, d8d7 53 …)로 2fcf vs baseline 재판정

## Case 4
확인된 인사이트
- 공개 최저 801.467 = baseline fleet 18 (팀 기록 없음).
- fleet 17은 18보다 나쁨, +4.8 CI [4.2, 5.4] [ev:c4.vsbest.03b542a9976c=worse]
- 별도 시드 baseline 평균 802.96, p95 809.02, 공개−별도 −1.5(과적합 작음).
검증 필요 가설
- 트리·통로 탐색 개선 여부(search 데이터 없음, overnight-45 결과가 digest search에 안 잡힘)
- fleet 19 이상 효과 데이터 없음
다음 확인할 것
- 트리/통로 탐색 결과 집계 경로 확인, kaggle 317건 회수

## Case 5
확인된 인사이트
- 공개 최저 771.4315 = baseline fleet 23 (팀 기록 없음).
- 별도 시드 최고는 fleet 24(adjMean 785.47); 23은 1.76 나쁨, CI [1.11, 2.41] [ev:c5.vsbest.dd48afb957ba=worse]
- 22→23 −11.78 [ev:c5.effect.dd48afb957ba.19b730ac721c=a-better]; 23→24 이득은 작음(1.76)
- 24는 긴급비 328.56, 23은 342.54, 차량비 360 vs 345 (수치 관찰)
- baseline 공개−별도 −15.79(공개가 낙관적).
검증 필요 가설
- 긴급·대기 감소가 원인이라는 설명(로그 미확인)
- 트리 탐색 개선 여부, fleet 25 이상(데이터 없음)
다음 확인할 것
- fleet 25 평가, 트리 탐색, kaggle 회수
