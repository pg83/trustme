//@ compile-fail: error
trait Cycle { const MARK: usize; }
impl<T: Cycle> Cycle for T { const MARK: usize = 23; }
fn generic<T>() -> usize { <T as Cycle>::MARK }
fn main() {
    let _ = generic::<u8>();
}
