// Extracted from library/alloc/src/vec/mod.rs:1431
#![allow(unused)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::collections::TryReserveError;
        
        fn process_data(data: &[u32]) -> Result<Vec<u32>, TryReserveError> {
            let mut output = Vec::new();
        
            // Pre-reserve the memory, exiting if we can't
            output.try_reserve_exact(data.len())?;
        
            // Now we know this can't OOM in the middle of our complex work
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
