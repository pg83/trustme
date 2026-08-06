// Extracted from library/core/src/fmt/builders.rs:1123
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt;
        
        struct Foo(Vec<(String, i32)>);
        
        impl fmt::Debug for Foo {
            fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
                // Print at most two elements, abbreviate the rest
                let mut f = fmt.debug_map();
                let mut f = f.entries(self.0.iter().take(2).map(|&(ref k, ref v)| (k, v)));
                if self.0.len() > 2 {
                    f.finish_non_exhaustive()
                } else {
                    f.finish()
                }
            }
        }
        
        assert_eq!(
            format!("{:?}", Foo(vec![
                ("A".to_string(), 10),
                ("B".to_string(), 11),
                ("C".to_string(), 12),
            ])),
            r#"{"A": 10, "B": 11, ..}"#,
        );
        Ok(())
    }
    doctest().unwrap();
}
