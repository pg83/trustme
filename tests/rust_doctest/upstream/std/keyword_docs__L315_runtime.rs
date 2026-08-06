// Extracted from library/std/src/keyword_docs.rs:315
#![allow(unused)]
fn main() {
    let result = if true == false {
        "oh no"
    } else if "something" == "other thing" {
        "oh dear"
    } else if let Some(200) = "blarg".parse::<i32>().ok() {
        "uh oh"
    } else {
        println!("Sneaky side effect.");
        "phew, nothing's broken"
    };
}
