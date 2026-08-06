// Extracted from library/std/src/thread/local.rs:265
#![allow(unused)]
fn main() {
    thread_local! {
        pub static STATIC: String = String::from("I am");
    }
    
    assert_eq!(
        STATIC.with(|original_value| format!("{original_value} initialized")),
        "I am initialized",
    );
}
