#!/bin/sh
# 產生給 Ripes 用的兩個檔案到 asm/ida/build/。
#
# Ripes 沒有 .include，也沒有 .if（reports/ripes-notes.md 第 8、9 點），所以在這裡
# 串檔，順便決定要不要放 renderer：
#   ida_cli.s：tables.s + ida_data.s + ida.s，刪掉 ida.s 裡行尾是 #RENDER 的行。
#              CLI 量 --iret 用；指令和沒有 renderer 時完全相同。
#   ida_gui.s：tables.s + ida_data.s + ida.s + render.s。GUI 看 LED 動畫用。
# 順序不能換：ida_data.s 的 .word 會用到 tables.s 的標籤（第 11 點），
# 而 tables.s 放最前面，表格才會從偶數位址開始。
#
# 用法：asm/ida/build.sh [14 位狀態]
#   不給狀態就用 ida_data.s 裡的 input；給了就只替換產生出來的檔案，不改 ida_data.s。
set -e

root=$(cd "$(dirname "$0")/../.." && pwd)
dir=$root/asm/ida
out=$dir/build

state=$1
case "$state" in
    *[!0-9A-Za-z]*) echo "state may contain only letters and digits: $state" >&2; exit 2 ;;
esac

# ida_data.s，有給狀態就把 input: 下一行的字串換掉
data() {
    if [ -n "$state" ]; then
        sed "/^input:/{n;s/\"[^\"]*\"/\"$state\"/;}" "$dir/ida_data.s"
    else
        cat "$dir/ida_data.s"
    fi
}

mkdir -p "$out"
{
    cat "$root/host/oracle/tables.s"
    data
    sed '/#RENDER$/d' "$dir/ida.s"
} > "$out/ida_cli.s"
{
    cat "$root/host/oracle/tables.s"
    data
    cat "$dir/ida.s" "$dir/render.s"
} > "$out/ida_gui.s"
echo "wrote $out/ida_cli.s and $out/ida_gui.s"
