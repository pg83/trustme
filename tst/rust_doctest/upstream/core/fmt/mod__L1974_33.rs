// Extracted from library/core/src/fmt/mod.rs:1974
#![allow(unused)]
fn main() {
    use std::fmt::{self, Alignment};

    struct Foo;

    impl fmt::Display for Foo {
        fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
            let s = if let Some(s) = formatter.align() {
                match s {
                    Alignment::Left    => "left",
                    Alignment::Right   => "right",
                    Alignment::Center  => "center",
                }
            } else {
                "into the void"
            };
            write!(formatter, "{s}")
        }
    }

    assert_eq!(format!("{Foo:<}"), "left");
    assert_eq!(format!("{Foo:>}"), "right");
    assert_eq!(format!("{Foo:^}"), "center");
    assert_eq!(format!("{Foo}"), "into the void");
}
