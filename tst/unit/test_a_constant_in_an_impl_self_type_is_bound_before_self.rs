// crypto-bigint writes `impl Concat<{ U64::LIMBS * 1 }> for
// Uint<{ <U128>::LIMBS - U64::LIMBS * 1 }>`. The binding pass bound the
// impl's self type once before `Self` was in scope and once more after, the
// second time giving every constant inside it the impl's self type as its
// `Self`: `Uint<2>` inside the constant then carried the type that contains
// the constant, and the next pass to walk the type recursed until the stack
// ran out. In rustc a constant in an impl's self type that named `Self`
// would be a cycle (E0391); one in the trait arguments may name it.
pub struct Uint<const LIMBS: usize>;
impl<const LIMBS: usize> Uint<LIMBS> {
    pub const LIMBS: usize = LIMBS;
}
pub const fn nlimbs(bits: usize) -> usize {
    bits / 64
}
pub type U64 = Uint<{ nlimbs(64) }>;
pub type U128 = Uint<{ nlimbs(128) }>;

pub trait Concat<const HI: usize> {
    type Output;
    fn limbs() -> usize;
}
impl Concat<{ U64::LIMBS * 1 }> for Uint<{ <U128>::LIMBS - U64::LIMBS * 1 }> {
    type Output = U128;
    fn limbs() -> usize {
        Self::LIMBS
    }
}

impl Uint<{ <Uint<3>>::LIMBS - 1 }> {
    fn two() -> usize {
        Self::LIMBS * 10
    }
}

pub struct A;
impl A {
    pub const N: usize = 4;
}
pub trait Tagged<const N: usize> {
    fn tag() -> usize {
        N
    }
}
impl Tagged<{ Self::N }> for A {}

fn main() {
    let output: <U64 as Concat<1>>::Output = Uint::<2>;
    let _ = output;
    assert_eq!(<Uint<1> as Concat<1>>::limbs(), 1);
    assert_eq!(Uint::<2>::two(), 20);
    assert_eq!(<A as Tagged<4>>::tag(), 4);
}
