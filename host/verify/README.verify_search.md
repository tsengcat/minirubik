# host/verify/verify_search.c

## 用途

檢查 H3：

> Your search returns a solution whose length equals the exact distance, for every state, not merely a solution that works.

它對全部 3,674,160 個狀態，照 `host/oracle/ida.c` 的 `main` 的順序執行一遍：`read_state` → `solve` → `check_path`，再逐項檢查結果。作業要求報告 H3 的 wall-clock 時間，所以它也會印出跑完全部狀態花了多久。

| 段落 | 檢查什麼 |
| :--- | :--- |
| Reference | `solver.c` 的 self-test 通過；它的 BFS 走到全部狀態 |
| read_state | 每個狀態的 14 位數字都轉出正確的 (p, o)；Makefile 裡 9 個不合法的輸入都被拒絕 |
| H3 | 每個狀態都找得到解；用 `solver.c` 逐顆 cubie 的模型重播，解答會回到 solved；解的長度等於真正的距離 |
| T5 check | `ida.c` 的 `check_path` 接受每一個正確的解；把第一步換成同一面的下一種轉法之後，它會拒絕 |

**H3 的結果包含重播。** H3 要求回傳的是 "a solution"，回不到 solved 的路徑根本不是解，所以重播失敗也算 H3 失敗。只有長度和重播都正確，H3 才算通過。

**read_state 和 T5 check 是 T5 的前提和檢查本身。** `check_path` 用轉移表重播，只能證明 move 把 (p, o) 帶到 (0, 0)。要說成「把輸入的 cube 帶到 solved」，還需要 (p, o) 確實是輸入字串對應的狀態，這由 read_state 那一段證明；另一個前提「轉移表等於 cube 模型」由 `verify_tables` 的 H2 證明。T5 check 確認 `check_path` 真的會檢查：它要接受正確的解，也要拒絕錯的解，不能永遠回傳 1。

## 相依性

| 讀什麼 | 怎麼讀 | 用途 |
| :--- | :--- | :--- |
| `host/oracle/ida.c` | 編譯時 include，`main` 改名成 `ida_main`、不呼叫 | 被驗的程式。直接呼叫它的 `read_state`、`solve`、`check_path`，驗的就是 target 要照著翻譯的那份程式碼 |
| `host/oracle/tables.h` | 透過 `ida.c` include | 搜尋用的表 |
| `solver.c` | 編譯時 include，`main` 改名成 `solver_main`，不修改 | 標準答案，以及獨立重播用的模型 |

被驗的程式可以在編譯時用 `-DIDA_SOURCE='"..."'` 換掉。平常不用，只有 `host/test/run.sh` 會拿它換成故意改壞的版本，確認這些檢查抓得到錯。

## 怎麼做

1. **真正的距離**：和 `verify_tables.c` 的做法相同，程式碼也是從那裡複製過來的。沿著 `solver.c` 的 BFS 表記錄的「往 solved 的那一步」走，數走了幾步。
2. **列舉狀態**：外層 p = 0..5039、內層 o = 0..728，rank 跟著加 1，所以不需要除法。對每個狀態：
   1. 用 `solver.c` 的 `unrank_state` 轉成 14 位字串，經過 `read_state`，比對讀出的 (p, o) 是否等於列舉時的 (p, o)。
   2. 用讀出的 (p, o) 呼叫 `solve`，比對長度，並用 `solver.c` 的 `apply_move` 獨立重播 `stack[0..len-1]`。
   3. 呼叫 `check_path`，必須回傳 1。再把第一步換成同一面的下一種轉法，必須回傳 0，然後換回來。同一面不同的轉法作用在任何狀態上結果都不一樣，所以換過之後的路徑一定回不到 solved。
3. **計時**：計的是走過全部狀態的迴圈，包含上面這些檢查，用 `time()`，精度到秒。要更精確的數字，可以在外面加 `time`。
4. **出錯時**：印出第一個出錯狀態的 14 位數字，可以直接拿去 `./host/oracle/ida` 重現。

## 編譯與執行

在 repo 根目錄，先用 `gen_tables` 產生 `tables.h`：

```sh
./host/oracle/gen_tables > host/oracle/tables.h
cc -O2 -std=c99 -Wall -Wextra -o host/verify/verify_search host/verify/verify_search.c
caffeinate -i time ./host/verify/verify_search
```

| 指令 | 做什麼 |
| :--- | :--- |
| `./host/verify/verify_search` | 全部 3,674,160 個狀態，需要幾分鐘。每 200,000 個狀態會在 stderr 印一行進度 |
| `./host/verify/verify_search --depth N` | 只跑距離是 N 的狀態（0–11），用來快速檢查。例如 `--depth 11` 跑全部 2,644 個最難的狀態 |

最後一行分開列出三項結果，例如 `H3 PASS, read_state PASS, T5 check PASS`。用 `--depth` 時只跑了一部分，不算完整的 H3，所以前面會加上 `distance N only:`。

全部通過時狀態碼是 0，有任何失敗是 1，參數錯誤是 2。
