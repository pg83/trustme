// Extracted from library/core/src/fmt/builders.rs:63
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt;
        
        struct Foo {
            bar: i32,
            baz: String,
        }
        
        impl fmt::Debug for Foo {
            fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
                fmt.debug_struct("Foo")
                   .field("bar", &self.bar)
                   .field("baz", &self.baz)
                   .finish()
            }
        }
        
        assert_eq!(
            format!("{:?}", Foo { bar: 10, baz: "Hello World".to_string() }),
            r#"Foo { bar: 10, baz: "Hello World" }"#,
        );
        Ok(())
    }
    doctest().unwrap();
}
