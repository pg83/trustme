#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
trait Same<T> {}
impl<T> Same<T> for T {}
struct Holder<const N: usize, const M: usize>;
trait Left { fn measure(&self) -> usize; }
trait Right { fn measure(&self) -> usize; }
impl<const N: usize, const M: usize> Left for Holder<N, M>
where [(); N + 1]: Same<[(); M + 2]>
{ fn measure(&self) -> usize { 829 } }
impl<const N: usize, const M: usize> Right for Holder<N, M>
where [(); N + 2]: Same<[(); M + 3]>
{ fn measure(&self) -> usize { 839 } }
fn main() {
    let value = Holder::<3, 2>;
    assert_eq!(Left::measure(&value), 829);
    assert_eq!(Right::measure(&value), 839);
}
