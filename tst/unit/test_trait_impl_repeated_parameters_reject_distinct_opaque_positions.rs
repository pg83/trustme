//@ compile-fail: No applicable methods
struct Pair<A, B>(A, B);
trait Same { fn width(&self) -> usize; }
impl<T: Copy> Same for Pair<T, T> { fn width(&self) -> usize { std::mem::size_of::<T>() } }
fn positions() -> (impl Copy, impl Copy) { (3u8, 5u8) }
fn main() { let (left, right) = positions(); let pair = Pair(left, right); let _ = pair.width(); }
