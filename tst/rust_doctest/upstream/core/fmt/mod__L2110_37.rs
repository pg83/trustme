// Extracted from library/core/src/fmt/mod.rs:2110
#![allow(unused)]
fn main() {
    use std::fmt;

    struct Foo(i32);

    impl fmt::Display for Foo {
        fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
            if formatter.sign_minus() {
                // You want a minus sign? Have one!
                write!(formatter, "-Foo({})", self.0)
            } else {
                write!(formatter, "Foo({})", self.0)
            }
        }
    }

    assert_eq!(format!("{:-}", Foo(23)), "-Foo(23)");
    assert_eq!(format!("{}", Foo(23)), "Foo(23)");
}
