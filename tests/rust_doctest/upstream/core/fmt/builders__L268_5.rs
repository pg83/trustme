// Extracted from library/core/src/fmt/builders.rs:268
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt;
        
        struct Foo(i32, String);
        
        impl fmt::Debug for Foo {
            fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
                fmt.debug_tuple("Foo")
                   .field(&self.0)
                   .field(&self.1)
                   .finish()
            }
        }
        
        assert_eq!(
            format!("{:?}", Foo(10, "Hello World".to_string())),
            r#"Foo(10, "Hello World")"#,
        );
        Ok(())
    }
    doctest().unwrap();
}
