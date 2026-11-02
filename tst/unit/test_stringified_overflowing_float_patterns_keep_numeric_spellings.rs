//@ compile-variant: -O
#![allow(overflowing_literals)]
macro_rules! pattern_text { ($pattern:pat) => { stringify!($pattern) }; }
fn main() {
    let text = pattern_text!(1e9999 | -1e9999);
    let compact: String = text.chars().filter(|c| !c.is_whitespace()).collect();
    assert_eq!(compact, "1e9999|-1e9999");
}
