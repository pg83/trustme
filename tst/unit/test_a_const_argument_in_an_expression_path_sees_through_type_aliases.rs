// crypto-bigint calls `self.optimized_bingcd_::<{ U64::BITS }, { U64::LIMBS },
// { U128::LIMBS }>(..)` with `type U64 = Uint<{ nlimbs(64) }>`. The anonymous
// constant in a method's or a function's generic arguments is a body like any
// other, and the alias in it names the aliased type; our alias pass only
// entered the constants of generic arguments in types and array lengths, so
// the binding pass met `U64` and died on "TypeAlias encountered after
// `Resolve Type Aliases`".
pub struct Uint<const L: usize>;
impl<const L: usize> Uint<L> {
    pub const BITS: u32 = 64 * L as u32;
    fn f<const K: u32>(&self) -> u32 { K }
}
pub type U64 = Uint<1>;
pub type U128 = Uint<2>;
struct S;
impl S {
    fn f<const K: u32>(&self) -> u32 { K }
}
fn g<const K: u32>() -> u32 { K }
fn main() {
    assert_eq!(S.f::<{ U64::BITS }>(), 64);
    assert_eq!(g::<{ U128::BITS }>(), 128);
    assert_eq!(<U64>::f::<{ U128::BITS + 1 }>(&Uint), 129);
}
