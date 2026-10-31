use std::sync::atomic::{AtomicU32, Ordering};
static VALUE: AtomicU32 = AtomicU32::new(17);
fn main() {
    let first = &VALUE;
    let second = first;
    assert_eq!(first.load(Ordering::SeqCst), 17);
    assert_eq!(second.fetch_add(20, Ordering::SeqCst), 17);
    assert_eq!((first.load(Ordering::SeqCst), second.load(Ordering::SeqCst)), (37, 37));
}
