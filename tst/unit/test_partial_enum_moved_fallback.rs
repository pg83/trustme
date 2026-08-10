use std::sync::atomic::{AtomicUsize, Ordering};

struct Tracked<'a>(&'a AtomicUsize);

impl Drop for Tracked<'_> {
    fn drop(&mut self) {
        self.0.fetch_sub(1, Ordering::SeqCst);
    }
}

fn keep_unmatched<'a>(
    slot: &mut Option<Tracked<'a>>,
    value: Option<Tracked<'a>>,
    take: bool,
) -> Option<Tracked<'a>> {
    match value {
        Some(value) if take => Some(value),
        other => {
            *slot = other;
            None
        }
    }
}

fn main() {
    let alive = AtomicUsize::new(0);
    alive.fetch_add(1, Ordering::SeqCst);
    let mut slot = None;
    let _ = keep_unmatched(&mut slot, Some(Tracked(&alive)), false);
    drop(slot);
    assert_eq!(alive.load(Ordering::SeqCst), 0);
}
