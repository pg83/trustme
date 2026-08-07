// Extracted from library/core/src/str/mod.rs:2632
#![allow(unused)]
fn main() {
    assert_eq!("11foo1bar11".trim_right_matches('1'), "11foo1bar");
    assert_eq!("123foo1bar123".trim_right_matches(char::is_numeric), "123foo1bar");

    let x: &[_] = &['1', '2'];
    assert_eq!("12foo1bar12".trim_right_matches(x), "12foo1bar");
}
