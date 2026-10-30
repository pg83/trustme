#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
struct Slots<const N: usize>([u8; N]);
const fn bump(n: usize) -> usize { n + 1 }
fn pass_call<const N: usize>(x: Slots<{ bump(N) }>) -> Slots<{ bump(N) }> { x }
fn pass_cast<const N: usize>(x: Slots<{ (N + 1) as usize }>) -> Slots<{ (N + 1) as usize }> { x }
fn pass_unary<const N: usize>(x: Slots<{ (!(N == 0)) as usize }>) -> Slots<{ (!(N == 0)) as usize }> { x }
struct Bridge;
impl Bridge {
    fn call<const UNUSED: usize, const N: usize>(x: Slots<{ bump(N) }>) -> Slots<{ bump(N) }> {
        pass_call::<N>(x)
    }
    fn cast<const UNUSED: usize, const N: usize>(x: Slots<{ (N + 1) as usize }>) -> Slots<{ (N + 1) as usize }> {
        pass_cast::<N>(x)
    }
    fn unary<const UNUSED: usize, const N: usize>(x: Slots<{ (!(N == 0)) as usize }>) -> Slots<{ (!(N == 0)) as usize }> {
        pass_unary::<N>(x)
    }
}
fn main() {
    assert_eq!(Bridge::call::<11, 2>(Slots([3; 3])).0, [3; 3]);
    assert_eq!(Bridge::cast::<13, 3>(Slots([5; 4])).0, [5; 4]);
    assert_eq!(Bridge::unary::<17, 2>(Slots([7; 1])).0, [7; 1]);
}
