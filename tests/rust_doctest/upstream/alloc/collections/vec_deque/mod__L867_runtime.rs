// Extracted from library/alloc/src/collections/vec_deque/mod.rs:867
#![allow(unused)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::collections::TryReserveError;
        use std::collections::VecDeque;
        
        fn process_data(data: &[u32]) -> Result<VecDeque<u32>, TryReserveError> {
            let mut output = VecDeque::new();
        
            // Pre-reserve the memory, exiting if we can't
            output.try_reserve_exact(data.len())?;
        
            // Now we know this can't OOM(Out-Of-Memory) in the middle of our complex work
            output.extend(data.iter().map(|&val| {
                val * 2 + 5 // very complicated
            }));
        
            Ok(output)
        }
        process_data(&[1, 2, 3]).expect("why is the test harness OOMing on 12 bytes?");
        Ok(())
    }
    doctest().unwrap();
}
