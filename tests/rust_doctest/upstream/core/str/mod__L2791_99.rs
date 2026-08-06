// Extracted from library/core/src/str/mod.rs:2791
#![allow(unused)]
fn main() {
    let mut s = String::from("Grüße, Jürgen ❤");
    
    s.make_ascii_uppercase();
    
    assert_eq!("GRüßE, JüRGEN ❤", s);
}
