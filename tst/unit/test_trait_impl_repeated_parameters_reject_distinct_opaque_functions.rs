//@ compile-fail: No applicable methods
struct Pair<A, B>(A, B);
trait Same { fn width(&self) -> usize; }
impl<T: Copy> Same for Pair<T, T> { fn width(&self) -> usize { std::mem::size_of::<T>() } }
fn red() -> impl Copy { 3u8 }
fn blue() -> impl Copy { 5u8 }
fn main() { let pair = Pair(red(), blue()); let _ = pair.width(); }
