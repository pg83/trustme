// Extracted from library/core/src/str/mod.rs:1762
#![allow(unused)]
fn main() {
    let v: Vec<&str> = "A.B.".split_terminator('.').collect();
    assert_eq!(v, ["A", "B"]);
    
    let v: Vec<&str> = "A..B..".split_terminator(".").collect();
    assert_eq!(v, ["A", "", "B", ""]);
    
    let v: Vec<&str> = "A.B:C.D".split_terminator(&['.', ':'][..]).collect();
    assert_eq!(v, ["A", "B", "C", "D"]);
}
