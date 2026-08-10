use std::sync::atomic::{AtomicUsize, Ordering};

static mut DROPS: usize = 0;
static ATOMIC_DROPS: AtomicUsize = AtomicUsize::new(0);

struct CountDrop(u8);

impl Drop for CountDrop {
    fn drop(&mut self) {
        unsafe {
            DROPS += 1;
        }
        ATOMIC_DROPS.fetch_add(1, Ordering::SeqCst);
    }
}

const VALUE: CountDrop = CountDrop(1);

impl CountDrop {
    const ASSOCIATED: CountDrop = CountDrop(2);
}

fn main() {
    assert_eq!(VALUE.0, 1);
    unsafe {
        assert_eq!(DROPS, 1);
    }
    assert_eq!(ATOMIC_DROPS.load(Ordering::SeqCst), 1);

    assert_eq!(CountDrop::ASSOCIATED.0, 2);
    unsafe {
        assert_eq!(DROPS, 2);
    }
    assert_eq!(ATOMIC_DROPS.load(Ordering::SeqCst), 2);
}
