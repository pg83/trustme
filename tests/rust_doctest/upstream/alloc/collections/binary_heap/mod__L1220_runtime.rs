// Extracted from library/alloc/src/collections/binary_heap/mod.rs:1220
#![allow(unused)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::collections::BinaryHeap;
        use std::collections::TryReserveError;
        
        fn find_max_slow(data: &[u32]) -> Result<Option<u32>, TryReserveError> {
            let mut heap = BinaryHeap::new();
        
            // Pre-reserve the memory, exiting if we can't
            heap.try_reserve(data.len())?;
        
            // Now we know this can't OOM in the middle of our complex work
            heap.extend(data.iter());
        
            Ok(heap.pop())
        }
        find_max_slow(&[1, 2, 3]).expect("why is the test harness OOMing on 12 bytes?");
        Ok(())
    }
    doctest().unwrap();
}
