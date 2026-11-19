# 시드별 운송 요청

시드마다 **요청이 언제, 어느 지점에서, 어디로 발생했는지** 담았습니다. `seed-11.csv.gz`는 공식 로그로부터 저장소에 이미 복원해 둔 입력을 풀어 쓴 파일입니다. `seed-12.csv.gz`부터 `seed-15.csv.gz`까지는 이 저장소의 C++ 엔진이 생성한 로컬 요청입니다. 공식 사이트의 같은 번호 시드와 일치한다는 뜻은 아닙니다.

열은 `seed,request_index,arrival_min,origin_node,destination_node,urgent`입니다. 요청 번호와 노드 번호는 0부터 시작합니다. 노드 번호는 [케이스 데이터](../../docs/case.json)의 `nodes` 배열 인덱스입니다. 시각의 단위는 분이며 엔진의 값을 다시 읽을 수 있는 정밀도로 기록했습니다. 표의 해시는 압축을 푼 **줄바꿈 LF인 CSV** 기준 SHA-256(Secure Hash Algorithm 256-bit)입니다.

| 시드 | 요청 수 | CSV SHA-256 |
| --- | ---: | --- |
| 11 | 8,453 | `84ef768b010b6518b79926b6b838a9da6ac753e329e97567b40fdb12b8b1b5b9` |
| 12 | 8,349 | `4c374a16c74b5c2e680549f6b37520fd6a25f1a2f7ed9fd4bb2c928ef523f8f9` |
| 13 | 8,698 | `55a467ddb98d5de47352d52a03440e7f7d9015768a7591d2ac63a5b83d8374be` |
| 14 | 8,623 | `5c5e5a377611f7f3db0c8a047dbfcbc3d0e0f7359e051c40c14084e90d6bbe33` |
| 15 | 8,564 | `42d685d04a99fee6043016dc59bf78189eddfec6d876dcb2ea2945016fd049d3` |

압축을 풀면 일반 CSV입니다. 같은 목록을 다시 내보내려면 저장소 루트에서 엔진을 빌드하고 `--export-arrivals`를 실행합니다. Windows에서는 `build/agv_opt.exe`를 사용합니다.

```sh
node tools/build.mjs native
build/agv_opt --export-arrivals --seeds 12 > seed-12.csv
```

`--seeds`를 생략하면 11~15를 한 CSV로 출력합니다. 이 명령은 요청 생성까지만 수행합니다. 평가 명령은 같은 생성 코드를 사용하고, 시드 11은 기존 `docs/seed11.json`을 읽습니다.
