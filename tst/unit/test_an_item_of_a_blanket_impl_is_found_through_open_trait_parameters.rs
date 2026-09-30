// crypto-bigint's tests call `FixedMontyForm::<{ U256::LIMBS }>::
// multi_exponentiate(&pairs)`: the trait is found among those in scope, and
// its only impl is a blanket one, `impl<T, E, B> MultiExponentiate<E, B> for
// T where E: Bounded, ..`. rustc asks whether the trait may hold with fresh
// inference variables for its parameters, and `?E: Bounded` is ambiguous,
// not unimplemented. We stood the parameters in as rigid unknowns, found
// `E: Bounded` to hold for none, and reported "Failed to find impl".
pub struct Uint<const L: usize>(pub [u64; L]);
impl<const L: usize> Uint<L> {
    pub const LIMBS: usize = L;
}
pub type U256 = Uint<4>;
pub trait Bounded {
    const BITS: u32;
}
impl<const L: usize> Bounded for Uint<L> {
    const BITS: u32 = 64 * L as u32;
}
pub trait Unsigned {}
impl<const L: usize> Unsigned for Uint<L> {}
pub trait PowBoundedExp<E> {
    fn pow_bounded_exp(&self, e: &E, bits: u32) -> Self;
}
pub trait Pow<E> {
    fn pow(&self, e: &E) -> Self;
}
impl<T: PowBoundedExp<E>, E: Unsigned> Pow<E> for T {
    fn pow(&self, e: &E) -> Self {
        self.pow_bounded_exp(e, 0)
    }
}
pub trait MultiExpBounded<E, B>: Pow<E> + Sized
where
    B: AsRef<[(Self, E)]> + ?Sized,
{
    fn meb(b: &B, bits: u32) -> Self;
}
pub trait MultiExp<E, B>: Pow<E> + Sized
where
    B: AsRef<[(Self, E)]> + ?Sized,
{
    fn multi_exponentiate(b: &B) -> Self;
}
impl<T, E, B> MultiExp<E, B> for T
where
    T: MultiExpBounded<E, B>,
    E: Bounded,
    B: AsRef<[(Self, E)]> + ?Sized,
{
    fn multi_exponentiate(b: &B) -> Self {
        Self::meb(b, E::BITS)
    }
}
pub struct F<const L: usize>(pub u64);
impl<const L: usize, const R: usize> PowBoundedExp<Uint<R>> for F<L> {
    fn pow_bounded_exp(&self, _: &Uint<R>, bits: u32) -> Self {
        F(self.0 + bits as u64)
    }
}
impl<const N: usize, const L: usize, const R: usize> MultiExpBounded<Uint<R>, [(Self, Uint<R>); N]> for F<L> {
    fn meb(b: &[(Self, Uint<R>); N], bits: u32) -> Self {
        F(b[0].0 .0 + bits as u64 + N as u64)
    }
}
impl<const L: usize, const R: usize> MultiExpBounded<Uint<R>, [(Self, Uint<R>)]> for F<L> {
    fn meb(b: &[(Self, Uint<R>)], bits: u32) -> Self {
        F(b.len() as u64 + bits as u64)
    }
}
fn main() {
    let be = [(F::<4>(1), Uint::<4>([0; 4]))];
    let r = F::<{ U256::LIMBS }>::multi_exponentiate(&be);
    assert_eq!(r.0, 1 + 256 + 1);
    let r = F::<{ U256::LIMBS }>::multi_exponentiate(be.as_slice());
    assert_eq!(r.0, 1 + 256);
}
