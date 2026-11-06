//@ compile-fail: error
trait Cycle { const MARK: usize; }
impl<T: Cycle> Cycle for T { const MARK: usize = 13; }
fn main() {
    let _ = <u8 as Cycle>::MARK;
}
