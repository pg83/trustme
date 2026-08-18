// `#[define_opaque(X)]` may name a plain alias of the opaque one. The two are
// the same type, but only the opaque one is the path an erased type records.
#![feature(type_alias_impl_trait)]

type Opaque2 = impl Sized;
type Opaque<'a> = Opaque2;

#[define_opaque(Opaque)]
fn define<'a>() -> Opaque<'a> {}

type Direct = impl Sized;

#[define_opaque(Direct)]
fn define_direct() -> Direct {
    7u8
}

fn main() {
    define();
    let _ = define_direct();
}
