// Extracted from library/core/src/fmt/builders.rs:221
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt;
        
        struct Bar {
            bar: i32,
            baz: String,
        }
        
        impl fmt::Debug for Bar {
            fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
                fmt.debug_struct("Bar")
                   .field("bar", &self.bar)
                   .field("baz", &self.baz)
                   .finish() // You need to call it to "finish" the
                             // struct formatting.
            }
        }
        
        assert_eq!(
            format!("{:?}", Bar { bar: 10, baz: "Hello World".to_string() }),
            r#"Bar { bar: 10, baz: "Hello World" }"#,
        );
        Ok(())
    }
    doctest().unwrap();
}
