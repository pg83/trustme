// Extracted from library/core/src/convert/mod.rs:438
#![allow(unused)]
fn main() {
    fn is_hello<T: Into<Vec<u8>>>(s: T) {
       let bytes = b"hello".to_vec();
       assert_eq!(bytes, s.into());
    }
    
    let s = "hello".to_string();
    is_hello(s);
}
