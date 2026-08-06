// Extracted from library/alloc/src/collections/vec_deque/mod.rs:595
#![allow(unused)]
#![feature(try_with_capacity)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        #[allow(unused)]
        fn example() -> Result<(), std::collections::TryReserveError> {
        use std::collections::VecDeque;
        
        let deque: VecDeque<u32> = VecDeque::try_with_capacity(10)?;
        Ok(()) }
    }
    doctest().unwrap();
}
