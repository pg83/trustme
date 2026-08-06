// Extracted from library/core/src/primitive_docs.rs:444
#![allow(unused)]
fn main() {
    let s = String::from("love: ❤️");
    let v: Vec<char> = s.chars().collect();
    
    assert_eq!(12, size_of_val(&s[..]));
    assert_eq!(32, size_of_val(&v[..]));
}
