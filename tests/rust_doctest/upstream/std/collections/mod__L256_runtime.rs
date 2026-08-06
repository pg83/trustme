// Extracted from library/std/src/collections/mod.rs:256
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        let vec = vec![1, 2, 3, 4];
        for x in vec.iter().rev() {
           println!("vec contained {x:?}");
        }
        Ok(())
    }
    doctest().unwrap();
}
