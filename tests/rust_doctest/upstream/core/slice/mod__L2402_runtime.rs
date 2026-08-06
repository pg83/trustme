// Extracted from library/core/src/slice/mod.rs:2402
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        let v = [10, 40, 30, 20, 60, 50];
        
        for group in v.splitn(2, |num| *num % 3 == 0) {
            println!("{group:?}");
        }
        Ok(())
    }
    doctest().unwrap();
}
