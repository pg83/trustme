#![feature(iter_macro, yield_expr)]

trait Family<T> {}

trait Finish {
    type Unit;
}

impl<T: ?Sized> Finish for T {
    type Unit = ();
}

mod nested {
    use std::iter::iter;

    struct Left<'a>(&'a u8);
    struct Right<'a>(&'a u16);

    pub fn check() {
        let make = iter! {
            || -> <dyn for<'a> super::Family<(Left<'a>, self::Right<'a>)> as super::Finish>::Unit {
                yield 37u16;
            }
        };
        let mut values = make();
        assert_eq!(values.next(), Some(37));
        assert_eq!(values.next(), None);
    }
}

fn main() {
    nested::check();
}
