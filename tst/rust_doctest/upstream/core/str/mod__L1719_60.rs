// Extracted from library/core/src/str/mod.rs:1719
#![allow(unused)]
fn main() {
    let v: Vec<&str> = "abc1defXghi".rsplit(|c| c == '1' || c == 'X').collect();
    assert_eq!(v, ["ghi", "def", "abc"]);
}
