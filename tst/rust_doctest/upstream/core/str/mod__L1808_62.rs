// Extracted from library/core/src/str/mod.rs:1808
#![allow(unused)]
fn main() {
    let v: Vec<&str> = "A.B.".rsplit_terminator('.').collect();
    assert_eq!(v, ["B", "A"]);

    let v: Vec<&str> = "A..B..".rsplit_terminator(".").collect();
    assert_eq!(v, ["", "B", "", "A"]);

    let v: Vec<&str> = "A.B:C.D".rsplit_terminator(&['.', ':'][..]).collect();
    assert_eq!(v, ["D", "C", "B", "A"]);
}
