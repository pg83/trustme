// Extracted from library/core/src/fmt/builders.rs:799
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt;
        
        struct Foo(Vec<i32>);
        
        impl fmt::Debug for Foo {
            fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
                // Print at most two elements, abbreviate the rest
                let mut f = fmt.debug_list();
                let mut f = f.entries(self.0.iter().take(2));
                if self.0.len() > 2 {
                    f.finish_non_exhaustive()
                } else {
                    f.finish()
                }
            }
        }
        
        assert_eq!(
            format!("{:?}", Foo(vec![1, 2, 3, 4])),
            "[1, 2, ..]",
        );
        Ok(())
    }
    doctest().unwrap();
}
