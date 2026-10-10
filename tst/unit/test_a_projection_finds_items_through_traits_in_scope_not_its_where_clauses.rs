// spade's `<T::Vertex as HasPosition>::Scalar::from(1.0)` under
// `<T::Vertex as HasPosition>::Scalar: Float`, with num-traits' `Float`
// imported and `type Scalar: SpadeNum` (`SpadeNum: From<f32>`). Upstream
// gathers where-clause candidates only for a type parameter (`assemble_probe`
// takes `ty::Param` alone); a projection is searched through the traits in
// scope, and `from` is `From::from`. We searched the where-clauses of any
// non-nominal type, supertraits included, found `NumCast::from` through
// `Float`, and the call produced an `Option`.
mod num {
    pub trait NumCast: Sized {
        fn from(n: f64) -> Option<Self>;
    }
    pub trait Float: NumCast + Copy {
        fn half(self) -> Self;
    }
    impl NumCast for f32 {
        fn from(n: f64) -> Option<f32> {
            Some(n as f32 + 100.0)
        }
    }
    impl Float for f32 {
        fn half(self) -> f32 {
            self / 2.0
        }
    }
}

use num::Float;

pub trait HasPosition {
    type Scalar: From<f32> + Copy;
}

pub struct V;

impl HasPosition for V {
    type Scalar = f32;
}

fn one<T: HasPosition>() -> T::Scalar
where
    T::Scalar: Float,
{
    <T as HasPosition>::Scalar::from(1.0).half()
}

fn quarter<T: HasPosition>(x: T::Scalar) -> T::Scalar
where
    T::Scalar: Float,
{
    <T as HasPosition>::Scalar::half(T::Scalar::half(x))
}

fn main() {
    assert_eq!(one::<V>(), 0.5);
    assert_eq!(quarter::<V>(1.0), 0.25);
}
