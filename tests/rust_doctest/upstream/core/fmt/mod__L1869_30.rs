// Extracted from library/core/src/fmt/mod.rs:1869
#![allow(unused)]
fn main() {
    use std::fmt;

    struct Foo;

    impl fmt::Display for Foo {
        fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
            formatter.write_str("Foo")
            // This is equivalent to:
            // write!(formatter, "Foo")
        }
    }

    assert_eq!(format!("{Foo}"), "Foo");
    assert_eq!(format!("{Foo:0>8}"), "Foo");
}
