// Extracted from library/std/src/ffi/os_str.rs:465
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::ffi::{OsStr, OsString};
        use std::collections::TryReserveError;
        
        fn process_data(data: &str) -> Result<OsString, TryReserveError> {
            let mut s = OsString::new();
        
            // Pre-reserve the memory, exiting if we can't
            s.try_reserve_exact(OsStr::new(data).len())?;
        
            // Now we know this can't OOM in the middle of our complex work
            s.push(data);
        
            Ok(s)
        }
        process_data("123").expect("why is the test harness OOMing on 3 bytes?");
        Ok(())
    }
    doctest().unwrap();
}
