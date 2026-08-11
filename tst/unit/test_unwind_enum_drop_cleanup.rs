use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicUsize, Ordering};

static DROPPED: AtomicUsize = AtomicUsize::new(0);

struct Probe {
    bit: usize,
    panic: bool,
}

impl Drop for Probe {
    fn drop(&mut self) {
        DROPPED.fetch_or(1 << self.bit, Ordering::SeqCst);
        if self.panic {
            panic!("panic in Probe::drop");
        }
    }
}

enum PanicBeforePayload {
    Active(Probe, Probe),
    Inactive,
}

impl Drop for PanicBeforePayload {
    fn drop(&mut self) {
        panic!("panic in PanicBeforePayload::drop");
    }
}

enum PanicInsidePayload {
    Active(Probe, Probe, Probe),
    Inactive,
}

fn main() {
    DROPPED.store(0, Ordering::SeqCst);
    let result = catch_unwind(AssertUnwindSafe(|| {
        drop(PanicBeforePayload::Active(
            Probe { bit: 0, panic: false },
            Probe { bit: 1, panic: false },
        ));
    }));
    assert!(result.is_err());
    assert_eq!(DROPPED.load(Ordering::SeqCst), 0b011);

    DROPPED.store(0, Ordering::SeqCst);
    let result = catch_unwind(AssertUnwindSafe(|| {
        drop(PanicInsidePayload::Active(
            Probe { bit: 0, panic: false },
            Probe { bit: 1, panic: true },
            Probe { bit: 2, panic: false },
        ));
    }));
    assert!(result.is_err());
    assert_eq!(DROPPED.load(Ordering::SeqCst), 0b111);
}
