//@ compile-fail: error
struct Carrier<const N: usize>;
trait Left<const N: usize> { fn measure(&self) -> usize; }
trait Right<const N: usize> { fn measure(&self) -> usize; }
impl<const N: usize> Left<N> for Carrier<N> { fn measure(&self) -> usize { N + 3 } }
impl<const N: usize> Right<N> for Carrier<N> { fn measure(&self) -> usize { N + 5 } }
fn unresolved<const N: usize>(value: Carrier<N>) -> usize { value.measure() }
fn main() { let _ = unresolved(Carrier::<7>); }
