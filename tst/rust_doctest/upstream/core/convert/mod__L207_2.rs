// Extracted from library/core/src/convert/mod.rs:207
#![allow(unused)]
fn main() {
    fn is_hello<T: AsRef<str>>(s: T) {
       assert_eq!("hello", s.as_ref());
    }

    let s = "hello";
    is_hello(s);

    let s = "hello".to_string();
    is_hello(s);
}
