// Extracted from library/alloc/src/string.rs:1324
#![allow(unused)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::collections::TryReserveError;
        
        fn process_data(data: &str) -> Result<String, TryReserveError> {
            let mut output = String::new();
        
            // Pre-reserve the memory, exiting if we can't
            output.try_reserve_exact(data.len())?;
        
            // Now we know this can't OOM in the middle of our complex work
            output.push_str(data);
        
            Ok(output)
        }
        process_data("rust").expect("why is the test harness OOMing on 4 bytes?");
        Ok(())
    }
    doctest().unwrap();
}
