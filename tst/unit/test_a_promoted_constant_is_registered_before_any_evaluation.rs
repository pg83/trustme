// crypto-bigint's `Int<LIMBS>` has `const MIN: Self = Self::MAX.not()`, and
// `not` takes `&self`: the borrow of `Self::MAX` in the generic constant is
// promoted into a constant of the `int` module. A test in `int::add` compares
// against `Int::<2>::MIN`, and its own promoted `&Int::<2>::MIN` is evaluated
// at once, which builds `MIN`'s body and meets the `int` module's promoted
// constant. The promoted items were registered module by module, each right
// before that module's evaluations, so a module visited earlier could not see
// a later one's: "Could not find value name int::const#0". Upstream's promoted
// bodies belong to their owners and are there whenever a query asks.
mod int {
    #[derive(Debug, PartialEq, Clone, Copy)]
    pub struct Int<const LIMBS: usize>(pub [u32; LIMBS]);

    impl<const LIMBS: usize> Int<LIMBS> {
        pub const MAX: Self = Self([u32::MAX >> 1; LIMBS]);
        pub const MIN: Self = Self::MAX.not();

        pub const fn not(&self) -> Self {
            let mut out = [0; LIMBS];
            let mut i = 0;
            while i < LIMBS {
                out[i] = !self.0[i];
                i += 1;
            }
            Self(out)
        }
    }

    pub mod add {
        pub fn check() {
            assert_eq!(super::Int::<2>::MIN, super::Int([1 << 31; 2]));
        }
    }
}

fn main() {
    int::add::check();
}
