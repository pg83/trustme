// Extracted from library/core/src/macros/mod.rs:573
#![allow(unused)]
#![allow(unused_imports)]
fn main() {
    use std::fmt::{self, Write as _};
    use std::io::{self, Write as _};
    
    struct Example;
    
    impl fmt::Write for Example {
        fn write_str(&mut self, _s: &str) -> core::fmt::Result {
             unimplemented!();
        }
    }
}
