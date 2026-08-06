// Extracted from library/core/src/str/mod.rs:2319
#![allow(unused)]
fn main() {
    assert_eq!("11foo1bar11".trim_matches('1'), "foo1bar");
    assert_eq!("123foo1bar123".trim_matches(char::is_numeric), "foo1bar");
    
    let x: &[_] = &['1', '2'];
    assert_eq!("12foo1bar12".trim_matches(x), "foo1bar");
}
