// Extracted from library/std/src/keyword_docs.rs:1023
#![allow(unused)]
fn main() {
    fn create_fn() -> impl Fn() {
        let text = "Fn".to_owned();
        move || println!("This is a: {text}")
    }
    
    let fn_plain = create_fn();
    fn_plain();
}
