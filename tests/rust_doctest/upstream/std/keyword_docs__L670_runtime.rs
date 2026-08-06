// Extracted from library/std/src/keyword_docs.rs:670
#![allow(unused)]
fn main() {
    if true == false {
        println!("oh no");
    } else if "something" == "other thing" {
        println!("oh dear");
    } else if let Some(200) = "blarg".parse::<i32>().ok() {
        println!("uh oh");
    } else {
        println!("phew, nothing's broken");
    }
}
