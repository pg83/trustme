// Extracted from library/core/src/fmt/mod.rs:2510
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt;
        
        struct Foo(Vec<i32>);
        
        impl fmt::Debug for Foo {
            fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
                fmt.debug_list().entries(self.0.iter()).finish()
            }
        }
        
        assert_eq!(format!("{:?}", Foo(vec![10, 11])), "[10, 11]");
        Ok(())
    }
    doctest().unwrap();
}
