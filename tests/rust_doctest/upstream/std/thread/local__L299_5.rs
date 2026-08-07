// Extracted from library/std/src/thread/local.rs:299
#![allow(unused)]
fn main() {
    thread_local! {
        pub static STATIC: String = String::from("I am");
    }

    assert_eq!(
        STATIC.try_with(|original_value| format!("{original_value} initialized")),
        Ok(String::from("I am initialized")),
    );
}
