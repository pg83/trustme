//@ compile-variant: -O
use std::sync::atomic::{AtomicU8, Ordering};
static COUNT: AtomicU8 = AtomicU8::new(0);
struct Unit;
impl Drop for Unit { fn drop(&mut self) { COUNT.fetch_add(1, Ordering::SeqCst); } }
fn main() {
    drop(Unit);
    drop(Unit);
    assert_eq!(COUNT.load(Ordering::SeqCst), 2);
}
