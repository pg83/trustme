#![feature(deref_patterns)]
#![allow(incomplete_features)]
fn classify(value: String, allow: bool) -> u8 {
    match value { deref!("wire") if allow => 7, deref!("wire") => 11, deref!("λ") => 13, _ => 17 }
}
fn main() {
    assert_eq!(classify(String::from("wire"), true), 7);
    assert_eq!(classify(String::from("wire"), false), 11);
    assert_eq!(classify(String::from("λ"), true), 13);
    assert_eq!(classify(String::from(""), false), 17);
}
