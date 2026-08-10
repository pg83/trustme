// Extracted from library/core/src/fmt/mod.rs:2044
#![allow(unused)]
fn main() {
    use std::fmt;

    struct Foo(f32);

    impl fmt::Display for Foo {
        fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
            if let Some(precision) = formatter.precision() {
                // If we received a precision, we use it.
                write!(formatter, "Foo({1:.*})", precision, self.0)
            } else {
                // Otherwise we default to 2.
                write!(formatter, "Foo({:.2})", self.0)
            }
        }
    }

    assert_eq!(format!("{:.4}", Foo(23.2)), "Foo(23.2000)");
    assert_eq!(format!("{}", Foo(23.2)), "Foo(23.20)");
}
