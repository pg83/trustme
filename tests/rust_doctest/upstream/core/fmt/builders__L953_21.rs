// Extracted from library/core/src/fmt/builders.rs:953
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt;
        
        struct Foo(Vec<(String, i32)>);
        
        impl fmt::Debug for Foo {
            fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
                fmt.debug_map()
                   .key(&"whole").value(&self.0) // We add the "whole" entry.
                   .finish()
            }
        }
        
        assert_eq!(
            format!("{:?}", Foo(vec![("A".to_string(), 10), ("B".to_string(), 11)])),
            r#"{"whole": [("A", 10), ("B", 11)]}"#,
        );
        Ok(())
    }
    doctest().unwrap();
}
