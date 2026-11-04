//@ compile-variant: -O
use std::sync::atomic::{AtomicUsize, Ordering};

static JUMPED: AtomicUsize = AtomicUsize::new(0);

#[inline(never)]
fn selected(flag: u64) {
    unsafe {
        std::arch::asm!(
            "test {flag}, {flag}",
            "jz {jump}",
            flag = in(reg) flag,
            jump = label {
                JUMPED.fetch_add(1, Ordering::SeqCst);
                return;
            },
            options(nostack, nomem),
        );
    }
}

fn main() {
    selected(std::hint::black_box(0));
    assert_eq!(JUMPED.load(Ordering::SeqCst), 1);
    selected(std::hint::black_box(1));
    assert_eq!(JUMPED.load(Ordering::SeqCst), 1);
}
