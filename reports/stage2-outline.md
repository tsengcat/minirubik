# Stage 2 報告大綱（工作用，不是最終稿）

這份只列出每一節要寫什麼、證據從哪裡來、需要你用自己的話回答哪些問題。最終稿要用英文，結構建議和 `stage1-draft.md` 一致。

作業對 Stage 2 的要求（原文重點）：

- Choose a representation and an algorithm that fit the target.
- Optimality is not negotiable: whatever you build still returns a shortest solution, and you can show that it does.
- Justify every choice against the numbers from stage 1, and against the budget.
- Ideas that turn out not to pay are worth reporting too.

作業規定以下部分必須是你自己的：表示法的選擇、搜尋設計與 admissibility 論證、所有量測、優化推理、筆記裡的分析。所以下面「要回答的問題」請用你自己的話寫，數字請自己重跑。

---

## 建議標題：Assignment 1, Stage 2: Choosing a Representation and an Algorithm

### 0. 從 Stage 1 帶過來的限制

整理成一張表，每一條都標明出處（Stage 1 第幾節，或作業的哪一條規定）：

| 限制 | 數值 | 出處 |
| :--- | :--- | :--- |
| 靜態資料上限 | 128 KiB | 作業 |
| 最差的距離 11 狀態，指令數上限 | 5×10⁷（`RV32_ISS`，`--iret`） | 作業 |
| 指令集 | RV32I：沒有 mul/div；不能用 heap、遞迴、浮點 | 作業 |
| Baseline 峰值記憶體 | 18,405,414 bytes | Stage 1 §7 |
| Ripes 每個 guest byte 佔多少 host 記憶體 | （Stage 1 §9.1 你量的值） | Stage 1 |
| `RV32_ISS` 每秒指令數 | （Stage 1 §9.2 你量的值） | Stage 1 |

要回答的問題：

- 這些限制裡，哪一條最先把 baseline 的做法排除掉？
- 為什麼「把完整距離表在 host 上算好再嵌進去」不行？（提示：作業的 precomputation rule，以及 4-bit 打包後仍有 1,794 KiB）

### 1. 狀態表示法

要寫的內容：

- 保留 baseline 的編碼方式：排列 p（0–5039）和方向 o（0–728）。
- 搜尋時把 (p, o) 分開保存，不合成 rank = p × 729 + o。

要回答的問題：

- 為什麼 p 和 o 可以分開更新？（`report.md` §4、你的 Stage 1 §4）
- 分開保存在 RV32I 上省掉了什麼運算？

### 2. 搜尋演算法

要寫的內容：

- 為什麼不用 BFS 或 A\*，而用 IDA\*。
- bound 從 h(root) 開始，每輪加 1。
- 剪枝：同一面不連續轉兩次（9 → 6 個分支）。

要回答的問題：

- A\* 需要什麼資料結構？那個資料結構在這裡會有多大？
- IDA\* 搜尋時的記憶體跟什麼成正比？在這題最多是多少？
- 為什麼每輪 bound 加 1 不會錯過最短解？
- 為什麼剪掉「同一面連轉兩次」不會錯過最短解？
- IDA\* 為什麼一定會停？（作業說面試會問「why the search terminates」）

建議引用：Korf 1985（IDA\*）、Russell & Norvig 第 3 章。

### 3. Heuristic

要寫的內容：

- 兩張投影距離表：`perm_dist[5040]`、`ori_dist[729]`，各自用小型 BFS 建出來。
- h = max(perm_dist[p], ori_dist[o])。

要回答的問題：

- 「投影」是什麼意思？為什麼原空間走一步，投影空間也剛好走一步？
- **為什麼投影空間的距離不會超過真實距離？**（admissibility 論證，作業要你自己寫；參考 Culberson & Schaeffer 1998）
- 為什麼兩個 admissible 的值取 max 之後仍然 admissible？
- 為什麼不是相加？

### 4. 記憶體預算

要寫的內容：每張表的大小和來源，以及總計。

| 表 | 元素型別 | 元素數 | bytes |
| :--- | :--- | ---: | ---: |
| `perm_turn` | uint16 | 3 × 5040 | |
| `ori_turn` | uint16 | 3 × 729 | |
| `perm_dist` | uint8 | 5040 | |
| `ori_dist` | uint8 | 729 | |
| 合計 | | | |
| 剩餘預算 | | | 131,072 − 合計 |

證據：`./host/test/ida_test` 的 `static data:` 那一行。

要回答的問題：

- 距離表最大值只有 7，用 4 bits 就夠，為什麼不打包成 nibble？（作業說讀一個 nibble 要多約 5 條指令）
- 剩下的預算有沒有用？沒用的話為什麼？用了的話換到什麼？

注意：這裡算的是 C 陣列的大小。Ripes 上實際的 `.data` 大小要到 Stage 4 再確認，而且表格以外的靜態資料（path、stack、測資）也要算進 128 KiB。

### 5. 設計的正確性

Stage 2 要證明的是「設計」是對的：IDA\* 加上這個 heuristic 一定回傳最短解。

要寫的內容：

- 最佳性的論證：bound 從 h(root) 開始、每輪加 1；h 不會高估；剪枝不會剪掉所有最短解。用你自己的話寫。
- 用 `ida_test` 這個原型在 host 上做的檢查：

| 檢查 | 做法 | 結果 | 指令 |
| :--- | :--- | :--- | :--- |
| H1 | 全部 3,674,160 個狀態都滿足 h(s) ≤ d(s) | （h > d 的數量） | `./host/test/ida_test` |
| H2 | 兩張距離表都填滿，驗證最大值和 solved 那格 | | `./host/test/ida_test` |
| H3 | 每個狀態的解，長度都等於真實距離，不只是「解得開」 | （失敗數、執行時間） | `time ./host/test/ida_test --all` |

寫報告時要注意：

- `ida_test` 的 `verify` 同時做兩件事：檢查長度等於真實距離（這是 H3），以及重新套用回到 solved（額外的檢查）。報告裡要分開寫。
- 要寫清楚對照用的 exact BFS 是哪一個：`ida_test` 的 `build_exact()` 是逐層掃描的版本，不是 `solver.c` 的 `build_table()`。

還可以補充的證據：

- Exact BFS 每層的狀態數，和 Jaap Scherphuis 網站公布的 2×2×2 HTM 分布逐層比對（作業參考資料有這個網站，請自己去對一次）。

H1 和 H3 的關係，作業 correctness gates 段落的最後一段已經說明了，請用你自己的話改寫。

要回答的問題：

- 如果 H3 對全部狀態都通過，為什麼還需要 heuristic 是 admissible？

H3 的執行時間作業要求回報。請在和 Stage 1 相同的電源設定下跑（電池供電、低耗電模式開啟），並用 `caffeinate -i` 防止電腦中途睡眠，把環境寫進報告。

### 6. 試過但沒採用的做法（選做，但作業鼓勵）

例如：只用 `perm_dist`、只用 `ori_dist`、用剩下的預算做更大的 pattern database。把 `ida_test.c` 的 `heuristic()` 換掉重跑，比較 depth-11 的最大節點數和表的大小。

注意：這一節的比較指標是節點數，也就是搜尋量。如果你決定 Stage 2 不談搜尋量，這一節就整節移到 Stage 3 以後。

| heuristic | 表的大小 | depth-11 最大 expanded | 結論 |
| :--- | ---: | ---: | :--- |
| max(perm, ori)（採用） | | | |
| 只用 perm | | | |
| 只用 ori | | | |

### 7. 帶到 Stage 3 的事

列出 `ida_test.c` 裡 target 不能直接用的寫法，作為 Stage 3 的起點：

- 遞迴
- `rank / 729`、`rank % 729`
- `rank_state` 裡乘以變數的乘法
- 64-bit 計數器
- 執行時建表（改成 `emit_tables` 產生的資料）

---

## AI usage disclosure 要補的內容

依實際情況寫，以下是這個階段發生過的事，供你參考：

- `host/test/ida_test.c`（原名 `stage2_probe.c`）由 Claude 撰寫，包括 IDA\*、heuristic、H1–H3 檢查和 KRE 估計。
- 之後由 Claude 重構：移除對 `solver.c` 的 include，改成複製需要的函式；改名；把輸出表格拆成 `host/test/emit_tables.c`；撰寫 `host/test/README.md`。
- Claude 另外寫了暫時性的檢查程式，驗證輸出的表（對照 `solver.c` 的 `quarter_turn`），並在 Ripes 上確認表格能被正確讀取。
- 報告中的數字由你自己重跑。
- 寫明你自己驗證、理解或修改了哪些部分，以及你接受這個設計的理由。

---

## 之後的階段才寫（不放進 Stage 2）

這一段只是先記下來，免得之後忘記，Stage 2 的報告不寫這些。

### Stage 3：每個節點的成本

作業要求 Stage 3「argue the effect from operation counts」，所以這些放在 Stage 3：

- 搜尋量：樣本 `21345671111111` 每輪 bound 的節點數（`./host/test/ida_test 21345671111111`）；全部距離 11 狀態的節點數最小、平均、最大值，以及最差的狀態（`./host/test/ida_test --depth11`）；最差狀態每輪 bound 的分布。
- 換算成每個節點的指令預算：5×10⁷ ÷ 最差的 generated、5×10⁷ ÷ 最差的 expanded。
- 估計 RV32I 上每個子節點要幾條指令，以及每個修改省下多少。
- 可以一起討論的問題：為什麼最後一輪 bound 的節點數比之前所有輪加起來還多；為什麼不同的距離 11 狀態成本可以差好幾倍；Korf–Reid–Edelkamp 的估計值和實際值為什麼不同。

每做一次修改，就記一列：

| 步驟 | 改了什麼 | 為什麼（哪一類：算術／記憶體／分支） | 每個子節點：改前 → 改後（估計指令數） | × 執行次數 | 驗證（節點數不變？H3？） |
| :--- | :--- | :--- | :--- | :--- | :--- |
| v0 | 改成 target 能跑的形式（無遞迴、無除法） | | | | |
| v1 | | | | | |

### 對最終程式的 correctness gates

作業的 H1–H4、T5–T7 是對「最後交出的程式」的檢查，用詞是 "your search"、"the program depends on"。所以寫出 target 程式之後還要做：

| Gate | 之後還要做的 | 時間點 |
| :--- | :--- | :--- |
| H1 | target 用的表只要和 `emit_tables` 輸出的是同一份，Stage 2 的結果就直接適用；換了 heuristic 就要重跑 | 換 heuristic 時 |
| H2 | 對實際嵌進程式的表（`tables.h` 或 `.s`）本身再檢查一次 | Stage 3、4 |
| H3 | 對 Stage 3 的 C search 重跑，並回報執行時間 | Stage 3 |
| H4 | 如果之後打包表格，要檢查讀取函式在奇數、偶數 index 都正確 | 打包時 |
| T5 | 在 Ripes 上，套用回傳的路徑會到 solved（程式內驗證） | Stage 4 |
| T6 | `21345671111111` 回傳 11 步的最短解 | Stage 4 |
| T7 | 3 個測資加上助教給的狀態，在 `RV32_ISS` 和至少一個 pipeline 模型都能重現 | Stage 4 |
