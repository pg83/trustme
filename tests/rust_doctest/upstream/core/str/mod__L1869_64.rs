// Extracted from library/core/src/str/mod.rs:1869
#![allow(unused)]
fn main() {
    let v: Vec<&str> = "abc1defXghi".splitn(2, |c| c == '1' || c == 'X').collect();
    assert_eq!(v, ["abc", "defXghi"]);
}
