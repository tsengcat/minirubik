# host/ida_draft/

這個目錄放只在電腦（host）上執行的工具，不會放上 Ripes。

## ida_test.c

### 用途

在把設計寫成 RV32I 組語之前，先在電腦上確認 Stage 2 選的設計行得通。

Baseline（`solver.c`）先用 BFS 建出全部 3,674,160 個狀態的表，再查表求解。Stage 1 已經說明這個做法在 Ripes 上不可行：峰值記憶體 18.4 MB，建表約 10⁹ 條指令。Stage 2 的設計改成：

- 不建完整的表，改用 **IDA\*** 搜尋最短解。
- heuristic 是 **h = max(perm_dist[p], ori_dist[o])**。兩張小表分別記錄「只看排列」和「只看方向」時離 solved 幾步，共 5,040 + 729 格。
- 同一面不連續轉兩次。

這個設計要先回答下面幾個問題，`ida_test` 就是用來回答它們的：

| 問題 | 做法 | 對應 gate |
| :--- | :--- | :--- |
| heuristic 會不會高估？ | 對全部 3,674,160 個狀態比較 h 和真正的距離 | H1 |
| 表是否完整？ | 檢查兩張距離表都填滿、solved 那格是 0、最大值是多少 | H2 |
| IDA\* 是否一定回傳最短解？ | 對每個狀態跑 IDA\*，檢查解的長度等於真正的距離，並重新套用確認會回到 solved | H3 |
| 搜尋量是否在預算內？ | 統計每個狀態 generated 和 expanded 的節點數，找出最差的狀態 | 估算 5×10⁷ 指令的門檻 |
| 表格是否放得進 128 KiB？ | 印出四張表的大小 | 靜態資料限制 |

「真正的距離」來自程式自己做的完整 BFS。這份資料只在 host 上當標準答案，不會放上 target。

### 它不是什麼

- **不是要放上 Ripes 的 solver。** 它用了遞迴、除法、`malloc`，還建了完整的距離表當答案，這些在 target 上都不允許。target 用的 solver 會在 Stage 3 另外寫。
- **不取代 `solver.c`。** Baseline 保持不動。

### 為什麼叫 ida_test

- **ida**：測試的對象是 IDA\* 搜尋，以及它用的 heuristic 表。
- **test**：它在 host 上測試這個設計是否正確、是否在預算內，不是要放上 target 的 solver。想試別的 heuristic，只要改 `heuristic()` 這個函式再重跑，就能比較不同設計的節點數。
- 放在 `host/ida_draft/`：只在電腦上執行。

### 和 solver.c 的關係

cube 模型（轉法、方向、rank 編碼）和 `solver.c` 相同，所以這裡驗證出的結果直接適用於 baseline 的編碼。它不 include `solver.c`，可以單獨編譯。

### 檔案結構

| 段落 | 內容 |
| :--- | :--- |
| Cube model | 逐顆 cubie 的狀態、轉法、rank 與 unrank、解析輸入 |
| The four search tables | 轉移表 `perm_turn`、`ori_turn`，heuristic 表 `perm_dist`、`ori_dist` |
| Search and checks | 完整 BFS（標準答案）、heuristic、IDA\*、驗證、各項報告 |

### 編譯與執行

在 repo 根目錄：

```sh
cc -O2 -std=c99 -Wall -Wextra -o host/ida_draft/ida_test host/ida_draft/ida_test.c
```

| 指令 | 做什麼 |
| :--- | :--- |
| `./host/ida_draft/ida_test` | H2、H1、成本估算，再解 `21345671111111` |
| `./host/ida_draft/ida_test <14 位數字>` | 只解這一個狀態，印出每輪 bound 的節點數 |
| `./host/ida_draft/ida_test --depth11` | 跑全部 2,644 個距離 11 的狀態 |
| `./host/ida_draft/ida_test --all` | H3：跑全部 3,674,160 個狀態，需要幾分鐘 |

### 名詞

- **generated**：被產生、算過 h 的節點數。
- **expanded**：通過剪枝（g + h ≤ bound）、要繼續展開子節點的節點數。

## emit_tables.c

### 用途

把 `ida_test.c` 建出來的四張表輸出成 C header（`tables.h`），讓 Stage 3 的 solver 直接把表當成資料使用，不用在執行時重建。

| 表 | 型別與大小 | 內容 |
| :--- | :--- | :--- |
| `perm_turn[f][p]` | uint16，3 × 5040 | 排列 p 轉一下第 f 面之後的排列 |
| `ori_turn[f][o]` | uint16，3 × 729 | 方向 o 轉一下第 f 面之後的方向 |
| `perm_dist[p]` | uint8，5040 | 只看排列時，離 solved 幾步 |
| `ori_dist[o]` | uint8，729 | 只看方向時，離 solved 幾步 |

f 是 0 = R、1 = B、2 = D，表裡只存四分之一圈。

### 為什麼 include ida_test.c

`emit_tables.c` 直接 include `ida_test.c`，呼叫它的 `build_tables()`。這樣輸出的表和 `ida_test` 用來做 H1–H3 驗證的表來自同一份程式碼，不會出現「驗證過的表」和「嵌進 solver 的表」不一致的情況。`ida_test.c` 本身只負責 IDA\* 的驗證與量測，不包含輸出功能。

### 編譯與執行

在 repo 根目錄：

```sh
cc -O2 -std=c99 -Wall -Wextra -o host/ida_draft/emit_tables host/ida_draft/emit_tables.c
./host/ida_draft/emit_tables > tables.h
```

輸出前會先檢查兩張距離表都已經填滿，沒填滿就不輸出，並以狀態碼 1 結束。
