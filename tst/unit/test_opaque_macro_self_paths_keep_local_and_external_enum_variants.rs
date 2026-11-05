//@ aux-build: macro_self_enum_payload.rs
#![feature(decl_macro)]
extern crate macro_self_enum_payload;

enum LocalPayload {
    Named { value: u16 },
    Pair(u16),
    Empty,
}

trait Views: Sized {
    fn named(value: u16) -> Self;
    fn pair(value: u16) -> Self;
    fn empty() -> Self;
    fn read(&self) -> u16;
}

macro make_views($ty:ty, $named:ident, $pair:ident, $empty:ident, $read:ident) {
    impl Views for $ty {
        fn $named(value: u16) -> Self { Self::Named { value } }
        fn $pair(value: u16) -> Self { Self::Pair(value) }
        fn $empty() -> Self { Self::Empty }
        fn $read(&self) -> u16 {
            match self {
                Self::Named { value } => *value,
                Self::Pair(value) => *value,
                Self::Empty => 0,
            }
        }
    }
}

make_views!(LocalPayload, named, pair, empty, read);
make_views!(macro_self_enum_payload::Payload, named, pair, empty, read);

fn exercise<T: Views>(named: u16, pair: u16) -> (u16, u16, u16) {
    (T::named(named).read(), T::pair(pair).read(), T::empty().read())
}

fn main() {
    assert_eq!(exercise::<LocalPayload>(571, 577), (571, 577, 0));
    assert_eq!(exercise::<macro_self_enum_payload::Payload>(587, 593), (587, 593, 0));
}
