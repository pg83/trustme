use std::sync::atomic::{AtomicU32, Ordering};

fn main() {
    let value = AtomicU32::new(13);
    assert_eq!(value.load(Ordering::Acquire), 13);
    value.store(19, Ordering::Release);
    assert_eq!(value.fetch_add(7, Ordering::AcqRel), 19);
    assert_eq!(value.fetch_sub(3, Ordering::Relaxed), 26);
    assert_eq!(value.compare_exchange(23, 31, Ordering::SeqCst, Ordering::Acquire), Ok(23));
    assert_eq!(value.compare_exchange(23, 37, Ordering::SeqCst, Ordering::Relaxed), Err(31));
    assert_eq!(value.load(Ordering::Relaxed), 31);
}
