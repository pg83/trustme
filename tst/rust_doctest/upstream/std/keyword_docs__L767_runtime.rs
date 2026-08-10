// Extracted from library/std/src/keyword_docs.rs:767
#![allow(unused)]
fn main() {
    fn thing_returning_closure() -> impl Fn(i32) -> bool {
        println!("here's a closure for you!");
        |x: i32| x % 3 == 0
    }
}
