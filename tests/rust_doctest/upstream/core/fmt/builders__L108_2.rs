// Extracted from library/core/src/fmt/builders.rs:108
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt;
        
        struct Bar {
            bar: i32,
            another: String,
        }
        
        impl fmt::Debug for Bar {
            fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
                fmt.debug_struct("Bar")
                   .field("bar", &self.bar) // We add `bar` field.
                   .field("another", &self.another) // We add `another` field.
                   // We even add a field which doesn't exist (because why not?).
                   .field("nonexistent_field", &1)
                   .finish() // We're good to go!
            }
        }
        
        assert_eq!(
            format!("{:?}", Bar { bar: 10, another: "Hello World".to_string() }),
            r#"Bar { bar: 10, another: "Hello World", nonexistent_field: 1 }"#,
        );
        Ok(())
    }
    doctest().unwrap();
}
