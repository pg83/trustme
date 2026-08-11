//@ compile-flags: -O

use std::sync::atomic::{AtomicUsize, Ordering};

static DROPS: AtomicUsize = AtomicUsize::new(0);

struct Probe;

impl Drop for Probe {
    fn drop(&mut self) {
        DROPS.fetch_add(1, Ordering::SeqCst);
    }
}

enum FiveVariants {
    A(Probe),
    B(Probe),
    C(Probe),
    D(Probe),
    E(Probe),
}

fn main() {
    drop(FiveVariants::A(Probe));
    drop(FiveVariants::B(Probe));
    drop(FiveVariants::C(Probe));
    drop(FiveVariants::D(Probe));
    drop(FiveVariants::E(Probe));
    assert_eq!(DROPS.swap(0, Ordering::SeqCst), 5);

    drop([Probe, Probe, Probe, Probe, Probe, Probe, Probe]);
    assert_eq!(DROPS.load(Ordering::SeqCst), 7);
}
