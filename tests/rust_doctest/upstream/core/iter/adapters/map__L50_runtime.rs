// Extracted from library/core/src/iter/adapters/map.rs:50
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        let mut c = 0;
        
        for pair in ['a', 'b', 'c'].into_iter()
                                       .map(|letter| { c += 1; (letter, c) })
                                       .rev() {
            println!("{pair:?}");
        }
        Ok(())
    }
    doctest().unwrap();
}
