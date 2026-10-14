# host/verify/

這個目錄放驗證用的程式，只在電腦（host）上執行，不會放上 Ripes。

## verify_tables.c

### 用途

對 `host/oracle/gen_tables` 產生的表格檔案檢查 H2、H4、H1。檢查的對象是要放進組語的 `tables.s`，不是 gen_tables 執行時記憶體裡的表。

| 段落 | 檢查什麼 |
| :--- | :--- |
| Layout | 四張表都在，指令（`.half`、`.byte`）和格數正確；`.half` 表的位址對齊；和 `tables.h` 逐格相同；檔案裡沒有其他表；總大小不超過 131,072 bytes |
| Reference | `solver.c` 的 self-test 通過；它的 BFS 走到全部 3,674,160 個狀態 |
| H2 | 兩張轉移表的每一面都是 0..size−1 的排列，而且在全部狀態、每一面上都和 `solver.c` 的模型一致。兩張距離表都填滿、solved 那格是 0、最大值以及每一格都等於標準答案 |
| H4 | 目前沒有打包的表，所以不適用 |
| H1 | 全部狀態都滿足 h ≤ d |

### 相依性

| 讀什麼 | 怎麼讀 | 用途 |
| :--- | :--- | :--- |
| `host/oracle/tables.s` | 執行時從參數讀入、解析 | 所有檢查都在這份資料上做 |
| `host/oracle/tables.h` | 編譯時 include | 和 `tables.s` 逐格比對 |
| `solver.c` | 編譯時 include，不修改 | 標準答案 |

它不 include `gen_tables.c`：被驗的程式只透過輸出的檔案被檢查。

### 標準答案

`solver.c` 的 `build_table()` 對每個狀態記錄「往 solved 走一步要轉哪一下」。沿著這些 move 走到 solved，數走了幾步，就是這個狀態真正的距離。每個狀態只會被走過一次，走到距離已知的狀態就停下來，再把一路上的距離補回去。

兩張距離表的標準答案：因為一次轉動對排列 p 和方向 o 的影響互不相干，`perm_dist[p]` 應該等於「排列是 p 的所有狀態中，真正距離的最小值」，`ori_dist[o]` 也一樣。

### H1 用的 heuristic

搜尋的程式還沒寫，所以 `verify_tables.c` 先自己寫了一份 `heuristic()`，讀的是 `tables.s` 裡的表。搜尋寫好之後，要換成搜尋本身的那一份。

### 編譯與執行

在 repo 根目錄，先產生表格檔案，再編譯（編譯時要用到 `tables.h`）：

```sh
./host/oracle/gen_tables > host/oracle/tables.h
./host/oracle/gen_tables --asm > host/oracle/tables.s
cc -O2 -std=c99 -Wall -Wextra -o host/verify/verify_tables host/verify/verify_tables.c
./host/verify/verify_tables host/oracle/tables.s
```

每項檢查印一行 `ok` 或 `FAIL`，最後一行是各個 gate 的結果。全部通過時狀態碼是 0，有任何失敗是 1，參數錯誤是 2。如果有表缺少或格數不對，就不會往下跑 gate。
