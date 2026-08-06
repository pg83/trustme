// Extracted from library/core/src/ops/deref.rs:243
#![allow(unused)]
fn main() {
    use std::ops::{Deref, DerefMut};
    
    struct DerefMutExample<T> {
        value: T
    }
    
    impl<T> Deref for DerefMutExample<T> {
        type Target = T;
    
        fn deref(&self) -> &Self::Target {
            &self.value
        }
    }
    
    impl<T> DerefMut for DerefMutExample<T> {
        fn deref_mut(&mut self) -> &mut Self::Target {
            &mut self.value
        }
    }
    
    let mut x = DerefMutExample { value: 'a' };
    *x = 'b';
    assert_eq!('b', x.value);
}
