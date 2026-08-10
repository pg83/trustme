// Extracted from library/std/src/keyword_docs.rs:737
#![allow(unused)]
fn main() {
    struct Example {
        number: i32,
    }

    trait Thingy {
        fn do_thingy(&self);
    }

    impl Thingy for Example {
        fn do_thingy(&self) {
            println!("doing a thing! also, number is {}!", self.number);
        }
    }
}
