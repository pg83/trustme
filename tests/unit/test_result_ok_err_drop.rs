use std::sync::atomic::{AtomicUsize, Ordering};

struct Tracked<'a>(&'a AtomicUsize);

impl Drop for Tracked<'_> {
    fn drop(&mut self) {
        self.0.fetch_sub(1, Ordering::SeqCst);
    }
}

fn discard_err<T, E>(value: Result<T, E>) -> Option<T> {
    match value {
        Ok(value) => Some(value),
        Err(_) => None,
    }
}

fn main() {
    let alive = AtomicUsize::new(0);
    alive.fetch_add(1, Ordering::SeqCst);
    let value: Result<(), Tracked<'_>> = Err(Tracked(&alive));
    let _ = discard_err(value);
    assert_eq!(alive.load(Ordering::SeqCst), 0);
}
