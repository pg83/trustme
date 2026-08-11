use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicUsize, Ordering};

static CLONED_DROPS: AtomicUsize = AtomicUsize::new(0);

struct Capture {
    bit: usize,
    cloned: bool,
    panic_on_clone: bool,
}

impl Clone for Capture {
    fn clone(&self) -> Self {
        if self.panic_on_clone {
            panic!("panic in Capture::clone");
        }
        Capture {
            bit: self.bit,
            cloned: true,
            panic_on_clone: false,
        }
    }
}

impl Drop for Capture {
    fn drop(&mut self) {
        if self.cloned {
            CLONED_DROPS.fetch_or(1 << self.bit, Ordering::SeqCst);
        }
    }
}

fn main() {
    let first = Capture { bit: 0, cloned: false, panic_on_clone: false };
    let second = Capture { bit: 1, cloned: false, panic_on_clone: true };
    let closure = move || {
        let _ = (&first, &second);
    };

    let result = catch_unwind(AssertUnwindSafe(|| {
        let _ = closure.clone();
    }));
    assert!(result.is_err());
    assert_eq!(CLONED_DROPS.load(Ordering::SeqCst), 0b01);
}
