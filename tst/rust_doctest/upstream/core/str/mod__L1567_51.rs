// Extracted from library/core/src/str/mod.rs:1567
#![allow(unused)]
fn main() {
    let v: Vec<&str> = "abc1defXghi".split(|c| c == '1' || c == 'X').collect();
    assert_eq!(v, ["abc", "def", "ghi"]);
}
