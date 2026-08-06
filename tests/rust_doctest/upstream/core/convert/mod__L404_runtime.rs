// Extracted from library/core/src/convert/mod.rs:404
#![allow(unused)]
#![allow(non_local_definitions)]
fn main() {
    struct Wrapper<T>(Vec<T>);
    impl<T> From<Wrapper<T>> for Vec<T> {
        fn from(w: Wrapper<T>) -> Vec<T> {
            w.0
        }
    }
}
