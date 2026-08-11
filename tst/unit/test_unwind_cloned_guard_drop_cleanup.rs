use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicUsize, Ordering};

static DROPS: AtomicUsize = AtomicUsize::new(0);

struct DropProbe {
    bit: usize,
    panic: bool,
}

impl Drop for DropProbe {
    fn drop(&mut self) {
        let previous = DROPS.fetch_or(self.bit, Ordering::SeqCst);
        assert_eq!(previous & self.bit, 0, "value dropped twice");
        if self.panic {
            panic!("panic in cloned guard drop");
        }
    }
}

fn exits_from_cloned_guard(value: u32) -> u32 {
    let _remaining = DropProbe { bit: 1, panic: false };
    let _panics = DropProbe { bit: 2, panic: true };
    let _dropped_before_panic = DropProbe { bit: 4, panic: false };

    match value {
        0 | 1 if return 16 => 0,
        _ => 17,
    }
}

fn main() {
    let result = catch_unwind(AssertUnwindSafe(|| exits_from_cloned_guard(1)));
    assert!(result.is_err());
    assert_eq!(DROPS.load(Ordering::SeqCst), 7);
}
