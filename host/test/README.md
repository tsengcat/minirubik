# host/test/

這個目錄放驗證程式本身的測試。驗證程式只在正確的檔案上通過還不夠，還要確認它在檔案有錯時真的會失敗。所以這裡放了幾個**故意改壞**的檔案，每個都只改一個地方，各自對應一項應該抓到它的檢查。

## 檔案

| 檔案 | 改壞了什麼 | 誰來驗 | 應該抓到它的檢查 |
| :--- | :--- | :--- | :--- |
| `tables_ori_dist_over.s` | `ori_dist[1]` 加了 3，heuristic 會高估 | `verify_tables` | H2：`ori_dist` 每一格等於標準答案；H1：h ≤ d |
| `tables_perm_turn_swapped.s` | `perm_turn` 的 R 面有兩格對調。仍然是一對一的排列，只有拿全部狀態和模型比對才抓得到 | `verify_tables` | H2：轉移表和 `solver.c` 的模型一致 |
| `ida_missing_turn.c` | `next_child` 的 `turn < 2` 寫成 `turn < 1`，從來不試 R'、B'、D' | `verify_search` | H3：有狀態在 11 步內找不到解 |
| `ida_wrong_move.c` | 找到解時，把第一步換成同一面的下一種轉法。長度不變，但 move 是錯的 | `verify_search` | H3：用 `solver.c` 重播回不到 solved；T5 check：`check_path` 拒絕了它回傳的解 |
| `ida_check_always_ok.c` | `check_path` 不重播、直接回傳 1，所以不管搜尋回傳什麼，T5 都會通過 | `verify_search` | T5 check：換掉第一步的錯誤路徑也被接受 |
| `ida_read_wrong_p.c` | `read_state` 裡 `p × 5 + c2` 寫成 `p × 4 + c2`，大部分狀態會被讀成另一個排列 | `verify_search` | read_state：讀出的 (p, o) 不對 |

最後一個檔案也說明了為什麼 `read_state` 要另外驗：它的 T5 check 是通過的。`check_path` 從同一組錯的 (p, o) 開始重播，仍然會回到 (0, 0)，所以「(p, o) 是輸入對應的狀態」這個前提只能在 host 上另外證明。

- 兩個 `.s` 是用 `host/oracle/tables.s` 改一格產生的完整檔案。檔頭和改壞的那一行都有 `BROKEN ON PURPOSE` 註解。
- 四個 `ida_*.c` 是 `host/oracle/ida.c` 的完整副本。和原本只差三個地方：檔頭的說明、改壞的那一行，以及 `#include "tables.h"` 改成 `"../oracle/tables.h"`，因為檔案不在 `host/oracle/` 裡。
- 如果 `tables.s` 或 `ida.c` 之後改了，這些檔案也要照同樣的方式重新產生，否則它們會因為其他差異而失敗，測不到原本要測的那一項。

## run.sh

一次跑完全部測試：

1. **對照組**：`verify_tables` 驗真正的 `tables.s`、`verify_search --depth 11` 驗真正的 `ida.c`，都必須通過。這樣才能確定後面的失敗是因為檔案改壞了，不是驗證程式本身就壞了。
2. **改壞的檔案**：每個都必須回傳 1，而且輸出要有預期的那幾行 FAIL。每個檔案只執行一次，再對同一份輸出檢查。預期的文字都包含 `FAIL`，所以通過的那一行（例如 `0 with no solution`）不會被誤認成抓到錯。
   - 驗證 `ida_*.c` 時，`verify_search` 會用 `-DIDA_SOURCE` 編譯成另一個執行檔，換掉被驗的搜尋。
3. 最後印出「幾項符合預期、幾項不符」。全部符合時回傳 0，否則回傳 1。

執行檔都編在暫存目錄，跑完就刪掉。

## 執行

先用 `gen_tables` 產生 `tables.h` 和 `tables.s`，然後在任何目錄都可以執行：

```sh
sh host/test/run.sh
```

目前共 18 項預期。全部大約 20 秒，主要花在 5 次 `verify_search --depth 11`。
