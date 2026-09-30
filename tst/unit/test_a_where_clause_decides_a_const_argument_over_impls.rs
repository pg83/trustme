// crypto-bigint's tests: inside `fn run_tests<const LIMBS: usize, const
// DOUBLE: usize>() where Uint<LIMBS>: Concat<LIMBS, Output = Uint<DOUBLE>>`,
// `test(Uint::ONE, Uint::ONE)` leaves `test`'s const parameters to the
// where-clause, as rustc's winnowing prefers it to the impls. Relating the
// where-clause's head took a pending equality of constants for a
// normalization still to be made, which only a constant yet to be evaluated
// is; binding `LIMBS` into the goal's variable is not.
#[derive(Clone, Copy, PartialEq, Debug)]
pub struct Uint<const L: usize>(pub [u64; L]);
impl<const L: usize> Uint<L> {
    pub const ONE: Self = {
        let mut limbs = [0; L];
        limbs[0] = 1;
        Uint(limbs)
    };
}
pub trait Concat<const HI: usize> {
    type Output;
    fn concat(&self, hi: &Uint<HI>) -> Self::Output;
}
impl Concat<1> for Uint<1> {
    type Output = Uint<2>;
    fn concat(&self, hi: &Uint<1>) -> Uint<2> {
        Uint([self.0[0], hi.0[0]])
    }
}
impl Concat<2> for Uint<2> {
    type Output = Uint<4>;
    fn concat(&self, hi: &Uint<2>) -> Uint<4> {
        Uint([self.0[0], self.0[1], hi.0[0], hi.0[1]])
    }
}
fn test<const LIMBS: usize, const DOUBLE: usize>(lhs: Uint<LIMBS>, rhs: Uint<LIMBS>) -> usize
where
    Uint<LIMBS>: Concat<LIMBS, Output = Uint<DOUBLE>>,
{
    let wide: Uint<DOUBLE> = lhs.concat(&rhs);
    wide.0.len()
}
fn run_tests<const LIMBS: usize, const DOUBLE: usize>() -> usize
where
    Uint<LIMBS>: Concat<LIMBS, Output = Uint<DOUBLE>>,
{
    test(Uint::ONE, Uint::ONE)
}
fn main() {
    assert_eq!(run_tests::<1, 2>(), 2);
    assert_eq!(run_tests::<2, 4>(), 4);
}
