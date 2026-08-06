// Extracted from library/core/src/fmt/builders.rs:721
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt;
        
        struct Foo(Vec<i32>, Vec<u32>);
        
        impl fmt::Debug for Foo {
            fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
                fmt.debug_list()
                   .entry(&self.0) // We add the first "entry".
                   .entry(&self.1) // We add the second "entry".
                   .finish()
            }
        }
        
        assert_eq!(
            format!("{:?}", Foo(vec![10, 11], vec![12, 13])),
            "[[10, 11], [12, 13]]",
        );
        Ok(())
    }
    doctest().unwrap();
}
