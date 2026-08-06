// Extracted from library/core/src/clone.rs:112
#![allow(unused)]
fn main() {
    struct Generate<T>(fn() -> T);
    
    // Automatically derived
    impl<T: Copy> Copy for Generate<T> { }
    
    // Automatically derived
    impl<T: Clone> Clone for Generate<T> {
        fn clone(&self) -> Generate<T> {
            Generate(Clone::clone(&self.0))
        }
    }
}
