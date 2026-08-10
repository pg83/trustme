// Extracted from library/core/src/str/mod.rs:2329
#![allow(unused)]
fn main() {
    assert_eq!("1foo1barXX".trim_matches(|c| c == '1' || c == 'X'), "foo1bar");
}
