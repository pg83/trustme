// Extracted from library/std/src/sync/barrier.rs:154
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::sync::Barrier;
        
        let barrier = Barrier::new(1);
        let barrier_wait_result = barrier.wait();
        println!("{:?}", barrier_wait_result.is_leader());
        Ok(())
    }
    doctest().unwrap();
}
