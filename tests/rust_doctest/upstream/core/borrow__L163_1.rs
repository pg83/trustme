// Extracted from library/core/src/borrow.rs:163
#![allow(unused)]
fn main() {
    use std::borrow::Borrow;
    
    fn check<T: Borrow<str>>(s: T) {
        assert_eq!("Hello", s.borrow());
    }
    
    let s = "Hello".to_string();
    
    check(s);
    
    let s = "Hello";
    
    check(s);
}
