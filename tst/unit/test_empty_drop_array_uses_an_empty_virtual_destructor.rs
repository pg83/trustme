use std::sync::atomic::{AtomicU32, Ordering};
static DROPS: AtomicU32 = AtomicU32::new(0);
struct Leaf;
impl Drop for Leaf { fn drop(&mut self) { DROPS.fetch_add(1, Ordering::Relaxed); } }
trait Length { fn length(&self) -> usize; }
impl<T, const N: usize> Length for [T; N] { fn length(&self) -> usize { N } }
fn main() {
    let empty: [Leaf; 0] = [];
    let erased: Box<dyn Length> = Box::new(empty);
    assert_eq!(erased.length(), 0);
    drop(erased);
    assert_eq!(DROPS.load(Ordering::Relaxed), 0);
}
