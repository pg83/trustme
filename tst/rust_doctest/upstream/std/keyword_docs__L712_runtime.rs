// Extracted from library/std/src/keyword_docs.rs:712
#![allow(unused)]
fn main() {
    struct Example {
        number: i32,
    }

    impl Example {
        fn boo() {
            println!("boo! Example::boo() was called!");
        }

        fn answer(&mut self) {
            self.number += 42;
        }

        fn get_number(&self) -> i32 {
            self.number
        }
    }
}
