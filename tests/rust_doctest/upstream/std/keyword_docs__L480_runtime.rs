// Extracted from library/std/src/keyword_docs.rs:480
#![allow(unused)]
fn main() {
    fn standalone_function() {
        // code
    }

    pub fn public_thing(argument: bool) -> String {
        // code
        "".to_string()
    }

    struct Thing {
        foo: i32,
    }

    impl Thing {
        pub fn new() -> Self {
            Self {
                foo: 42,
            }
        }
    }
}
