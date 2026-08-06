// Extracted from library/core/src/fmt/mod.rs:1941
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt;
        
        struct Foo;
        
        impl fmt::Display for Foo {
            fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
                let c = formatter.fill();
                if let Some(width) = formatter.width() {
                    for _ in 0..width {
                        write!(formatter, "{c}")?;
                    }
                    Ok(())
                } else {
                    write!(formatter, "{c}")
                }
            }
        }
        
        // We set alignment to the right with ">".
        assert_eq!(format!("{Foo:G>3}"), "GGG");
        assert_eq!(format!("{Foo:t>6}"), "tttttt");
        Ok(())
    }
    doctest().unwrap();
}
