//@ compile-variant: -O
use std::sync::atomic::{AtomicU64, Ordering};
static DROPS: AtomicU64 = AtomicU64::new(0);
struct Mark(u8);
impl Drop for Mark { fn drop(&mut self) { DROPS.fetch_add(1 << self.0, Ordering::SeqCst); } }
fn main() {
    let [first, mut middle @ .., last] = [Mark(0), Mark(1), Mark(2), Mark(3), Mark(4)];
    middle.swap(0, 2);
    assert_eq!([middle[0].0, middle[1].0, middle[2].0], [3, 2, 1]);
    drop(middle); drop(first); drop(last);
    assert_eq!(DROPS.load(Ordering::SeqCst), 31);
}
