// Extracted from library/core/src/fmt/builders.rs:369
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt;
        
        struct Foo(i32, String);
        
        impl fmt::Debug for Foo {
            fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
                fmt.debug_tuple("Foo")
                   .field(&self.0)
                   .finish_non_exhaustive() // Show that some other field(s) exist.
            }
        }
        
        assert_eq!(
            format!("{:?}", Foo(10, "secret!".to_owned())),
            "Foo(10, ..)",
        );
        Ok(())
    }
    doctest().unwrap();
}
