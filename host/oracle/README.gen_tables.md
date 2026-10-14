# host/oracle/

這個目錄放建表的程式，只在電腦（host）上執行，不會放上 Ripes。

## gen_tables.c

### 用途

從逐顆 cubie 的 cube 模型建出搜尋要用的四張表，輸出成兩種格式，讓 solver 直接把表當成資料使用，不用在執行時重建：

- `tables.h`：C header，給 host 上的 C 程式。
- `tables.s`：組語資料（`.half`、`.byte`），給 target 上的組語 solver。

兩個檔案都從同一份資料寫出。`host/verify/verify_tables` 會檢查兩者逐格相同。

| 表 | 型別與大小 | 內容 |
| :--- | :--- | :--- |
| `perm_turn[f][p]` | uint16，3 × 5040 | 排列 p 轉一下第 f 面之後的排列 |
| `ori_turn[f][o]` | uint16，3 × 729 | 方向 o 轉一下第 f 面之後的方向 |
| `perm_dist[p]` | uint8，5040 | 只看排列時，離 solved 幾步 |
| `ori_dist[o]` | uint8，729 | 只看方向時，離 solved 幾步 |

f 是 0 = R、1 = B、2 = D，表裡只存四分之一圈。

### 相依性

`gen_tables.c` 只用到 C 標準函式庫，不依賴 repo 裡的其他檔案。

cube 模型和建表的程式碼是從 `host/ida_draft/ida_test.c` 抽出來的，`host/ida_draft/` 保持原樣、不 include 這個檔案。

它只負責建表和輸出，不檢查表的內容。檢查由 `host/verify/verify_tables.c` 對輸出的檔案進行，詳見 `host/verify/README.verify_tables.md`。

### 檔案結構

| 段落 | 內容 |
| :--- | :--- |
| Cube model | 逐顆 cubie 的狀態、四分之一圈、rank 與 unrank，和 `solver.c` 相同 |
| The four search tables | `build_turn_tables`、`bfs_projection`、`build_tables` |
| Output | `emit()`（C header）、`emit_asm()`（組語資料）、`main` |

### 編譯與執行

在 repo 根目錄：

```sh
cc -O2 -std=c99 -Wall -Wextra -o host/oracle/gen_tables host/oracle/gen_tables.c
./host/oracle/gen_tables > host/oracle/tables.h
./host/oracle/gen_tables --asm > host/oracle/tables.s
```

### tables.s 的格式

- 開頭是 `.data`，接著每張表一個 label，後面是 `.half` 或 `.byte`，依記憶體順序排列。
- 兩張轉移表依面存放：先放 R 的整列，再放 B，最後放 D。所以 `perm_turn` 第 f 面的第 p 格，在第 `f × 5040 + p` 個 `.half`。
- 兩張 `.half` 表放在最前面，所以只要這個檔案從偶數位址開始，`lhu` 讀到的位址都是對齊的。
