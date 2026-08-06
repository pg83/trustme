// Extracted from library/core/src/clone.rs:91
#![allow(unused)]
fn main() {
    struct Generate<T>(fn() -> T);
    
    impl<T> Copy for Generate<T> {}
    
    impl<T> Clone for Generate<T> {
        fn clone(&self) -> Self {
            *self
        }
    }
}
