// Extracted from library/core/src/convert/mod.rs:416
#![allow(unused)]
fn main() {
    struct Wrapper<T>(Vec<T>);
    impl<T> Into<Vec<T>> for Wrapper<T> {
        fn into(self) -> Vec<T> {
            self.0
        }
    }
}
