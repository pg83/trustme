pub fn catch_old<F>(function: F) -> i32
where
    F: FnOnce() + std::panic::UnwindSafe,
{
    match std::panic::catch_unwind(function) {
        Ok(()) => 0,
        Err(_) => 42,
    }
}
