// Extracted from library/core/src/str/mod.rs:1653
#![allow(unused)]
fn main() {
    let v: Vec<&str> = "Mary had a little lamb\nlittle lamb\nlittle lamb."
        .split_inclusive('\n').collect();
    assert_eq!(v, ["Mary had a little lamb\n", "little lamb\n", "little lamb."]);
}
