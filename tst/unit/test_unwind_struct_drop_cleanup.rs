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

struct PanicBeforeFields(Probe, Probe);

impl Drop for PanicBeforeFields {
    fn drop(&mut self) {
        panic!("panic in PanicBeforeFields::drop");
    }
}

struct PanicBetweenFields(Probe, Probe, Probe);

fn main() {
    DROPPED.store(0, Ordering::SeqCst);
    let result = catch_unwind(AssertUnwindSafe(|| {
        drop(PanicBeforeFields(
            Probe { bit: 0, panic: false },
            Probe { bit: 1, panic: false },
        ));
    }));
    assert!(result.is_err());
    assert_eq!(DROPPED.load(Ordering::SeqCst), 0b011);

    DROPPED.store(0, Ordering::SeqCst);
    let result = catch_unwind(AssertUnwindSafe(|| {
        drop(PanicBetweenFields(
            Probe { bit: 0, panic: false },
            Probe { bit: 1, panic: true },
            Probe { bit: 2, panic: false },
        ));
    }));
    assert!(result.is_err());
    assert_eq!(DROPPED.load(Ordering::SeqCst), 0b111);

    DROPPED.store(0, Ordering::SeqCst);
    let result = catch_unwind(AssertUnwindSafe(|| {
        drop((
            Probe { bit: 0, panic: false },
            Probe { bit: 1, panic: true },
            Probe { bit: 2, panic: false },
        ));
    }));
    assert!(result.is_err());
    assert_eq!(DROPPED.load(Ordering::SeqCst), 0b111);
}
