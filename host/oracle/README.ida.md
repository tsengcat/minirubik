# host/oracle/ida.c

## 用途

只用四張表解一個 cube 狀態，回傳最短解。這是 target 組語 solver 的 C 藍本，之後的組語會照著它人工翻譯，所以每一個運算在 RV32I 上都要有直接對應的指令。

一次查詢分成四步：解析輸入 → 搜尋 → 印出解答 → 把解答套回輸入、確認回到 solved（T5），結果用狀態碼回報。入口只有 `main` 一個，target 的組語也照同一個順序翻譯。

## 規則

| 規則 | 原因 |
| :--- | :--- |
| 只 include `tables.h`（以及 `<stdint.h>`、`<stdio.h>`） | 表格之外不依賴任何東西 |
| 不用 `*`、`/`、`%`，查詢時只跑一次的輸入轉換也一樣 | RV32I 沒有乘除法 |
| 不遞迴、不用 heap、不用浮點 | target 的限制 |
| 轉移表的每一列用指標表 `perm_row[f]`、`ori_row[f]` 取得 | 不用 `face × 5040` 算位址 |
| 區域變數用 `unsigned`；只有放在記憶體的資料用 `uint16_t`、`uint8_t` | 對應 32-bit 暫存器，以及 `lhu`/`sh`、`lbu`/`sb` |
| 不用 `solver.c` 裡的名稱 | 元件 5 會同時 include `solver.c` 和這個檔案 |

## 一次查詢怎麼跑

```
main
 ├─ read_state(argv[1]) → p, o        不合法：回傳 2
 ├─ solve(p, o)         → length      找不到：回傳 1
 ├─ print_path(length)                印出 "R B' D2 ..."
 ├─ check_path(p, o, length)          回不到 solved：回傳 1（T5）
 └─ 回傳 0
```

### read_state：字串 → p、o

- **檢查輸入**：
  - 前 7 位必須是 1–7、不能重複，用 bitmask 記錄出現過的數字。
  - 後 7 位必須是 1–3、總和是 3 的倍數。每加一位，超過 3 就減 3，不用 mod。
  - 長度剛好 14。`'\0'` 減 `'1'` 會變成很大的 unsigned 數，所以字串太短也會被範圍檢查擋下。
- **p**：先數出 c0..c5，cᵢ 是「後面比第 i 顆小的個數」，再用 Horner 法合起來。乘以常數都寫成 shift 加 add：

  | 步驟 | 寫法 |
  | :--- | :--- |
  | p×6 + c1 | `(p << 2) + (p << 1) + c1` |
  | p×5 + c2 | `(p << 2) + p + c2` |
  | p×4 + c3 | `(p << 2) + c3` |
  | p×3 + c4 | `(p << 1) + p + c4` |
  | p×2 + c5 | `(p << 1) + c5` |

- **o**：前 6 個方向用 3 進位，`o = (o << 1) + o + digit`。
- 算出的 p、o 和 `solver.c` 的 `rank_state()` 算出的 rank ÷ 729、rank mod 729 相同。這裡不合成 rank，所以不需要除法。

### stack：取代遞迴

```c
typedef struct {
    uint16_t p, o;      /* 深度 g 的節點 */
    uint8_t face, turn; /* 正在試的 move：face 0-2，turn 0-2 = 轉 1-3 個四分之一圈 */
    uint8_t skip;       /* 走到這裡的那一步轉的面 */
} frame_t;              /* 8 bytes，組語裡用 g << 3 取位址 */
```

- 第 g 層正在試的子節點，就是 `stack[g + 1]` 的 p、o。
- 解答就是 `stack[0..len-1]` 的 `(face, turn)`。

### search(bound)：一輪深度優先搜尋

每一步只有三種動作：

1. **檢查子節點**：`limit = bound − g − 1`，是子節點最多還能走的步數。`perm_dist > limit` 或 `ori_dist > limit` 就剪掉。先查 `perm_dist`，超過就不用再讀 `ori_dist`，也不用算 max。
2. **通過剪枝**：
   - `limit == 0`：找到解，長度 g + 1。只有 solved 那一格在兩張表都是 0（H2 驗過每一格），所以通過剪枝就是 solved，不用另外判斷。
   - 否則：往下一層，從第一個不是 `skip` 的面開始試。
3. **被剪掉**：換下一個子節點（`next_child`）。
   - 同一面再轉一下：`child = row[face][child]`。所以 R、R2、R' 是同一列連查 1、2、3 次。
   - 三下都試過了，就換下一個面，跳過 `skip`。
   - 這一層都試完，就退回上一層；退到根還試完，表示這個 bound 沒有解。

子節點的順序和 `host/ida_draft/ida_test.c` 相同，所以兩邊找到的解答相同，走過的節點也相同。

### solve(p, o)：IDA\*

- bound 從 h(root) = max(`perm_dist[p]`, `ori_dist[o]`) 開始，每輪加 1。h 是 0 表示輸入就是 solved，長度 0。
- **為什麼是最短解**：h 不會高估（H1），所以長度 L 的解在 bound = L 那一輪一定會被找到；比 L 小的 bound 都已經試過、確定沒有解。
- **為什麼會停**：每一輪的深度最多 bound，分支數有限。bound 最多到 11，因為 diameter 是 11（H3）。超過 11 回傳 −1，這只是防呆。

### check_path(p, o, length)：T5

作業要求 T5（"Applying every returned path reaches the solved state"）在 target 上成立，並且要在程式內驗證、用狀態碼回報。所以 `main` 印出解答之後，會把 `stack[0..length-1]` 的每一步重新套回輸入的 (p, o)：每一步轉 `turn + 1` 次四分之一圈，用的是搜尋時同一組 `perm_row`、`ori_row`。最後必須是 (0, 0)，否則回傳 1。

這裡用轉移表重播，而不是另外放一份逐顆 cubie 的模型，這樣 `ida.c` 只多一個小函式，也沒有新的表。代價是它只能直接證明「這些 move 把 (p, o) 帶到 (0, 0)」。要說成「把輸入的 cube 帶到 solved」，還需要兩個前提，都在 host 上證明：

| 前提 | 由誰證明 |
| :--- | :--- |
| 轉移表等於逐顆 cubie 的模型 | H2：`verify_tables` 在全部狀態 × 3 面上比對過 |
| (p, o) 確實是輸入字串對應的狀態 | `verify_search`：全部狀態的 14 位字串經過 `read_state`，都得到正確的 (p, o) |

`check_path` 本身直接抓得到的，是搜尋的錯：記下來的 move 和實際走的不一致，或回傳的路徑最後不是 solved。它最多只做 11 步 × 3 次 × 2 次查表，跟預算相比可以忽略。

## 和 ida_test.c 的差異

| ida_test.c | ida.c | 原因 |
| :--- | :--- | :--- |
| `rank / 729`、`rank % 729` | p、o 從輸入直接算 | 不用除法 |
| `perm_turn[face][x]` | `perm_row[face][x]` | 不用 face × 5040 |
| `path[g] = face * 3 + t` | 解答存在 stack 的 face、turn | 不用乘法 |
| 遞迴的 `search()` | 自己管理的 stack | target 不能遞迴 |
| `g + max(a, b) > bound` | `a > limit \|\| b > limit` | 不用算 max，多數情況只讀一張表 |
| 通過剪枝後再判斷 solved | `limit == 0` 時通過剪枝就是 solved | 少一次比較，也不會走進最後一層 |
| generated、expanded 計數 | 沒有 | target 用 `--iret` 量 |

## 編譯與執行

在 repo 根目錄，先用 `gen_tables` 產生 `tables.h`：

```sh
./host/oracle/gen_tables > host/oracle/tables.h
cc -O2 -std=c99 -Wall -Wextra -o host/oracle/ida host/oracle/ida.c
./host/oracle/ida 21345671111111
```

輸出格式和 `solver.c` 相同：move 之間用一個空格隔開，最後換行；輸入是 solved 時印一個空行。

| 狀態碼 | 意思 |
| :--- | :--- |
| 0 | 解出來了 |
| 1 | 找不到解（不該發生）、解答套回去回不到 solved（T5 失敗），或 stdout 寫入失敗 |
| 2 | 參數不對，或輸入不是合法的狀態 |

## 給元件 5 用

元件 5（`host/verify/verify_search.c`）用 `#define main ida_main` 的方式 include 這個檔案。`ida_main` 不會被呼叫；它照 `main` 的順序直接呼叫 `read_state()`、`solve()`、`check_path()`，再從 `stack[0..len-1]` 讀出解答。這是 host 上的測試工具，不是第二個入口。
