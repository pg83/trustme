// Extracted from library/alloc/src/fmt.rs:178
#![allow(unused)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        println!("Hello {:^15}!", format!("{:?}", Some("hi"))); // => "Hello   Some("hi")   !"
        Ok(())
    }
    doctest().unwrap();
}
