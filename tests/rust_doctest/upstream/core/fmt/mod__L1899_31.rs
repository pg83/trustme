// Extracted from library/core/src/fmt/mod.rs:1899
#![allow(unused)]
fn main() {
    use std::fmt;

    struct Foo(i32);

    impl fmt::Display for Foo {
        fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
            formatter.write_fmt(format_args!("Foo {}", self.0))
        }
    }

    assert_eq!(format!("{}", Foo(-1)), "Foo -1");
    assert_eq!(format!("{:0>8}", Foo(2)), "Foo 2");
}
