//@ crate-type: lib

#![feature(type_alias_impl_trait)]

trait Equate {
    type Projection;
}

impl<T> Equate for T {
    type Projection = T;
}

trait Indirect {
    type Type;
}

impl<T, U: Equate<Projection = T>> Indirect for (T, U) {
    type Type = ();
}

mod nested {
    use super::*;

    type Opaque<'a> = impl Sized + 'a;

    #[define_opaque(Opaque)]
    fn annotation<'a: 'b, 'b: 'a>(_: Opaque<'a>) {
        let _ = None::<<(Opaque<'a>, &'b u8) as Indirect>::Type>;
    }
}
