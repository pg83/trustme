// Extracted from library/core/src/sync/atomic.rs:2532
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::sync::atomic::AtomicBool;
        let atomic_bool = AtomicBool::from(true);
        assert_eq!(format!("{atomic_bool:?}"), "true")
        Ok(())
    }
    doctest().unwrap();
}
