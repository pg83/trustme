// Extracted from library/core/src/iter/adapters/map.rs:33
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        let mut c = 0;
        
        for pair in ['a', 'b', 'c'].into_iter()
                                       .map(|letter| { c += 1; (letter, c) }) {
            println!("{pair:?}");
        }
        Ok(())
    }
    doctest().unwrap();
}
