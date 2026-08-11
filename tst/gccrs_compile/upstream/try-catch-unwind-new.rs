pub fn catch_new<F>(function: F) -> bool
where
    F: FnOnce() + std::panic::UnwindSafe,
{
    std::panic::catch_unwind(function).is_err()
}
