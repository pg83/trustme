// Extracted from library/core/src/str/mod.rs:2819
#![allow(unused)]
fn main() {
    let mut s = String::from("GRÜßE, JÜRGEN ❤");
    
    s.make_ascii_lowercase();
    
    assert_eq!("grÜße, jÜrgen ❤", s);
}
