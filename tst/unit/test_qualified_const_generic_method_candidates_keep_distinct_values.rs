struct Carrier<const N: usize>;
trait Left<const N: usize> { fn measure(&self) -> usize; }
trait Right<const N: usize> { fn measure(&self) -> usize; }
impl<const N: usize> Left<N> for Carrier<N> { fn measure(&self) -> usize { N + 3 } }
impl<const N: usize> Right<N> for Carrier<N> { fn measure(&self) -> usize { N + 5 } }
fn main() {
    let value = Carrier::<11>;
    assert_eq!(<Carrier<11> as Left<11>>::measure(&value), 14);
    assert_eq!(<Carrier<11> as Right<11>>::measure(&value), 16);
}
