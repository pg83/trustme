//@ compile-flags: -O

use std::panic::catch_unwind;
use std::sync::atomic::{AtomicUsize, Ordering};

static DROPS: AtomicUsize = AtomicUsize::new(0);

struct DropProbe(bool);

impl Drop for DropProbe {
    fn drop(&mut self) {
        DROPS.fetch_add(1, Ordering::SeqCst);
        if self.0 {
            panic!("panic in DropProbe::drop");
        }
    }
}

fn main() {
    let values = vec![DropProbe(false), DropProbe(true), DropProbe(false)];
    assert!(catch_unwind(move || drop(values.into_iter())).is_err());
    assert_eq!(DROPS.load(Ordering::SeqCst), 3);
}
