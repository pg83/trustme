// crypto-bigint 0.7.5 implements traits for `Uint<{ U64::LIMBS }>`-shaped
// types: impl headers with anonymous constants whose bodies name an
// associated const of a const-generic type. rustc's anonymous constant is
// an item of its own, and its body is resolved once, where it is written.
// Visiting the header's types again from the trait arguments walked the
// self type's constant's body again, with the impl's self type as `Self`:
// a constant inside that body took as its `Self` a type that holds the
// body itself, and the next walk over it never ended.
pub struct Uint<const LIMBS: usize>(pub [u64; LIMBS]);

impl<const LIMBS: usize> Uint<LIMBS> {
    pub const LIMBS: usize = LIMBS;
}

pub struct E<const L: usize>(pub [u8; L]);

pub trait Tr<X> {
    fn f(x: X) -> Self;
}

pub struct A<const N: usize>(pub [u8; N]);

impl Tr<E<{ Uint::<2>::LIMBS }>> for A<{ Uint::<3>::LIMBS }> {
    fn f(x: E<2>) -> Self {
        A([x.0[0], x.0[1], 7])
    }
}

fn main() {
    let a = <A<3> as Tr<E<2>>>::f(E([1, 2]));
    assert_eq!(a.0, [1, 2, 7]);
}
