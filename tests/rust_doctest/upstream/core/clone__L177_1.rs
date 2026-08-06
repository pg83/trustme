// Extracted from library/core/src/clone.rs:177
#![allow(unused)]
#![allow(noop_method_call)]
fn main() {
    let hello = "Hello"; // &str implements Clone
    
    assert_eq!("Hello", hello.clone());
}
