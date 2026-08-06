// Extracted from library/core/src/ops/deref.rs:116
#![allow(unused)]
fn main() {
    use std::ops::Deref;
    
    struct DerefExample<T> {
        value: T
    }
    
    impl<T> Deref for DerefExample<T> {
        type Target = T;
    
        fn deref(&self) -> &Self::Target {
            &self.value
        }
    }
    
    let x = DerefExample { value: 'a' };
    assert_eq!('a', *x);
}
