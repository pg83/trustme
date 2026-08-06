// Extracted from library/std/src/keyword_docs.rs:1570
#![allow(unused)]
fn main() {
    struct User {
        name: String,
        admin: bool,
    }
    
    impl User {
        pub fn new(name: String) -> Self {
            Self {
                name,
                admin: false,
            }
        }
    }
}
