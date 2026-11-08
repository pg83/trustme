use std::sync::atomic::{AtomicUsize, Ordering};

static DROPS: AtomicUsize = AtomicUsize::new(0);

struct Unit;

impl Drop for Unit {
    fn drop(&mut self) {
        DROPS.fetch_add(1, Ordering::SeqCst);
    }
}

fn early_return() -> u16 {
    let _unreached = Unit { ..{ return 31; } };
    0
}

fn main() {
    let base = Unit;
    let rebuilt = Unit { ..base };
    assert_eq!(DROPS.load(Ordering::SeqCst), 0);
    drop(rebuilt);
    assert_eq!(DROPS.load(Ordering::SeqCst), 1);
    drop(base);
    assert_eq!(DROPS.load(Ordering::SeqCst), 2);
    assert_eq!(early_return(), 31);
    assert_eq!(DROPS.load(Ordering::SeqCst), 2);
}
