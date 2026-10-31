#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
trait Family { const LENGTH: usize; }
struct Small;
struct Wide;
impl Family for Small { const LENGTH: usize = 2; }
impl Family for Wide { const LENGTH: usize = 4; }
#[inline(never)]
fn inspect<T: Family>(value: &[u16; T::LENGTH], index: usize) -> u16 where [(); T::LENGTH]: {
    value[index]
}
fn main() {
    assert_eq!(inspect::<Small>(&[37, 41], std::hint::black_box(1)), 41);
    assert_eq!(inspect::<Wide>(&[43, 47, 53, 59], std::hint::black_box(3)), 59);
}
