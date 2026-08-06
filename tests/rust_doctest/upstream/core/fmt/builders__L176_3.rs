// Extracted from library/core/src/fmt/builders.rs:176
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt;
        
        struct Bar {
            bar: i32,
            hidden: f32,
        }
        
        impl fmt::Debug for Bar {
            fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
                fmt.debug_struct("Bar")
                   .field("bar", &self.bar)
                   .finish_non_exhaustive() // Show that some other field(s) exist.
            }
        }
        
        assert_eq!(
            format!("{:?}", Bar { bar: 10, hidden: 1.0 }),
            "Bar { bar: 10, .. }",
        );
        Ok(())
    }
    doctest().unwrap();
}
