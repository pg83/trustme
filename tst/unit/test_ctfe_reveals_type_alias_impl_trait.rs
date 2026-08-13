#![feature(type_alias_impl_trait)]

type Opaque = impl Fn() -> usize;

#[define_opaque(Opaque)]
const fn make_opaque() -> Opaque {
    || 7
}

const VALUE: Opaque = make_opaque();

mod qualified {
    pub type Opaque = impl Fn() -> usize;

    #[define_opaque(self::Opaque)]
    pub const fn make_opaque() -> Opaque {
        || 11
    }
}

const QUALIFIED_VALUE: qualified::Opaque = qualified::make_opaque();

fn main() {
    assert_eq!(VALUE(), 7);
    assert_eq!(QUALIFIED_VALUE(), 11);
}
