// bigdecimal's `RadixType::Base` is bounded by `num_traits::PrimInt` - whose
// supertrait `NumCast` has `fn from<T: ToPrimitive>` - and by `From<bool>`, and
// writes `Self::Base::from(result >= ..)` with only `From` in scope. rustc takes
// the candidates for an associated item of a projection from the traits in
// scope (`assemble_probe` has bound candidates only for a type parameter); the
// bounds decide only whether they apply. The first bound declaring a `from`
// was taken, and `NumCast::from` wanted `bool: ToPrimitive`.
mod num {
    pub trait ToPrim {
        fn to_u64(&self) -> u64;
    }
    impl ToPrim for u8 {
        fn to_u64(&self) -> u64 {
            *self as u64
        }
    }

    pub trait NumCastLike: Sized {
        fn from<T: ToPrim>(n: T) -> Option<Self>;
    }
    pub trait Prim: NumCastLike + Copy {}

    impl NumCastLike for u32 {
        fn from<T: ToPrim>(n: T) -> Option<Self> {
            Some(n.to_u64() as u32 + 100)
        }
    }
    impl Prim for u32 {}
}

trait Radix {
    type Base: num::Prim + From<bool> + From<u8>;
    fn flag(b: bool) -> Self::Base {
        Self::Base::from(b)
    }
}

struct R;
impl Radix for R {
    type Base = u32;
}

fn main() {
    assert_eq!(R::flag(true), 1);
}
