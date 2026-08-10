// Extracted from library/core/src/str/mod.rs:1575
#![allow(unused)]
fn main() {
    let x = "||||a||b|c".to_string();
    let d: Vec<_> = x.split('|').collect();

    assert_eq!(d, &["", "", "", "", "a", "", "b", "c"]);
}
