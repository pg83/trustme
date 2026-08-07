// Extracted from library/core/src/fmt/mod.rs:2009
#![allow(unused)]
fn main() {
    use std::fmt;

    struct Foo(i32);

    impl fmt::Display for Foo {
        fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
            if let Some(width) = formatter.width() {
                // If we received a width, we use it
                write!(formatter, "{:width$}", format!("Foo({})", self.0), width = width)
            } else {
                // Otherwise we do nothing special
                write!(formatter, "Foo({})", self.0)
            }
        }
    }

    assert_eq!(format!("{:10}", Foo(23)), "Foo(23)   ");
    assert_eq!(format!("{}", Foo(23)), "Foo(23)");
}
