// crypto-bigint's `impl<const LIMBS: usize> OddUintXgcdOutput<LIMBS>`
// destructures `let Self { .. } = self;`, where `OddUintXgcdOutput` is a
// type alias of a struct. `Self` in an impl is the impl's self type with
// the alias expanded; we looked the alias itself up as the pattern's struct
// and died on "didn't point to a struct or union (TypeAlias)".
mod gcd {
    pub type OddOutput<const L: usize> = Output<L, u32>;
    #[derive(Clone, Copy)]
    pub struct Output<const L: usize, G: Copy> {
        pub gcd: G,
        pub x: [u8; L],
    }
    impl<const L: usize> OddOutput<L> {
        pub const fn make() -> Self {
            OddOutput { gcd: 1, x: [0; L] }
        }
        pub const fn split(self) -> (u32, usize) {
            let Self { gcd, x } = self;
            (gcd, x.len())
        }
    }

    pub type Pair = Tuple<u8>;
    pub struct Tuple<T>(pub T, pub T);
    impl Pair {
        pub fn sum(&self) -> u8 {
            let Self(a, b) = self;
            a + b
        }
    }

    pub type Marker = Unit;
    #[derive(PartialEq)]
    pub struct Unit;
    impl Marker {
        pub fn is_unit(&self) -> bool {
            matches!(self, Self)
        }
    }
}

fn main() {
    assert_eq!(gcd::OddOutput::<3>::make().split(), (1, 3));
    assert_eq!(gcd::Tuple(2, 5).sum(), 7);
    assert!(gcd::Unit.is_unit());
}
