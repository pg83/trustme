// Extracted from library/core/src/str/mod.rs:1560
#![allow(unused)]
fn main() {
    let v: Vec<&str> = "2020-11-03 23:59".split(&['-', ' ', ':', '@'][..]).collect();
    assert_eq!(v, ["2020", "11", "03", "23", "59"]);
}
