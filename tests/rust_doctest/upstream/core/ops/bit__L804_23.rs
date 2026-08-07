// Extracted from library/core/src/ops/bit.rs:804
#![allow(unused)]
fn main() {
    use std::ops::BitXorAssign;

    #[derive(Debug, PartialEq)]
    struct Personality {
        has_soul: bool,
        likes_knitting: bool,
    }

    impl BitXorAssign for Personality {
        fn bitxor_assign(&mut self, rhs: Self) {
            self.has_soul ^= rhs.has_soul;
            self.likes_knitting ^= rhs.likes_knitting;
        }
    }

    let mut personality = Personality { has_soul: false, likes_knitting: true };
    personality ^= Personality { has_soul: true, likes_knitting: true };
    assert_eq!(personality, Personality { has_soul: true, likes_knitting: false});
}
