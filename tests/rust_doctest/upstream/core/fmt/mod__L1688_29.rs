// Extracted from library/core/src/fmt/mod.rs:1688
#![allow(unused)]
fn main() {
    use std::fmt;
    
    struct Foo;
    
    impl fmt::Display for Foo {
        fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
            formatter.pad("Foo")
        }
    }
    
    assert_eq!(format!("{Foo:<4}"), "Foo ");
    assert_eq!(format!("{Foo:0>4}"), "0Foo");
}
