//@ compile-variant: -O
mod outer {
    pub const BASE: u16 = 719;
    pub mod inner {
        pub struct Pair<T> { pub value: T }
        impl<T> Pair<T> {
            pub fn new(value: T) -> Self { Self { value } }
            pub fn take(self) -> T { let Self { value } = self; value }
        }
        pub trait Reading { type Value; fn value(self) -> Self::Value; }
        impl Reading for Pair<u16> {
            type Value = u16;
            fn value(self) -> u16 { self.take() + super::BASE }
        }
        pub fn shifted() -> u16 {
            let pair = self::Pair::new(5u16);
            <Pair<u16> as Reading>::value(pair)
        }
    }
}
type Alias<T> = outer::inner::Pair<T>;
fn local<const N: usize>(value: u16) -> [u16; N] {
    let callback: fn(u16) -> u16 = |x| x + 3;
    let Alias { value } = Alias { value: callback(value) };
    [value; N]
}
fn main() {
    assert_eq!(outer::inner::shifted(), 724);
    assert_eq!(<Alias<u16>>::new(727).take(), 727);
    assert_eq!(local::<2>(733), [736, 736]);
}
