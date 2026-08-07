// Extracted from library/core/src/fmt/mod.rs:2167
#![allow(unused)]
fn main() {
    use std::fmt;

    struct Foo(i32);

    impl fmt::Display for Foo {
        fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
            assert!(formatter.sign_aware_zero_pad());
            assert_eq!(formatter.width(), Some(4));
            // We ignore the formatter's options.
            write!(formatter, "{}", self.0)
        }
    }

    assert_eq!(format!("{:04}", Foo(23)), "23");
}
