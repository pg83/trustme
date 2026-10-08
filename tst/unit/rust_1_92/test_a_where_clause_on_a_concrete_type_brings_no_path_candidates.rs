// A path `Type::item` finds the item among the type's inherent impls and the
// traits in scope; the where-clauses bring in candidates only for a type
// parameter (`assemble_inherent_candidates_from_param` in rustc's method
// probe applies to `ty::Param` alone). With `Wrap<T>: FloatLike` and
// `FloatLike: Cast`, `Wrap::<T>::from(..)` is still `From::from`, not
// `Cast::from`: lambert_w's `Complex::<T>::from(d_one)` under
// `Complex<T>: ComplexFloat` (whose supertrait `NumCast` has a `from`).
mod numeric {
    pub trait Cast: Sized {
        fn from(x: u8) -> Option<Self>;
    }
    pub trait FloatLike: Cast + Copy {
        fn magnitude(self) -> u8;
    }
}

#[derive(Clone, Copy)]
struct Wrap<T>(T);

impl<T> numeric::Cast for Wrap<T> {
    fn from(_: u8) -> Option<Self> {
        None
    }
}

impl<T: Copy> numeric::FloatLike for Wrap<T> {
    fn magnitude(self) -> u8 {
        7
    }
}

use numeric::FloatLike;

fn wrapped<T: Copy>(t: T) -> u8
where
    Wrap<T>: FloatLike,
{
    let w = Wrap::<T>::from(Wrap(t));
    w.magnitude()
}

fn main() {
    assert_eq!(wrapped(3u8), 7);
}
