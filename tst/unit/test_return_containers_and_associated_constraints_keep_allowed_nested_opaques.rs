use std::fmt::Debug;

fn wrapped() -> Option<impl Copy + PartialEq<u8> + Debug> {
    Some(19u8)
}

fn stream() -> impl Iterator<Item = impl Copy + PartialEq<u8> + Debug> {
    std::iter::once(23u8)
}

trait Family<T> {
    type Member<U>;
}

trait Carrier<T>: Family<T> {}

trait Api {
    fn equality(&self) -> impl Carrier<u16, Member<impl Clone> = u8>;
    fn bounded(&self) -> impl Carrier<u16, Member<impl Clone>: Send>;
}

fn main() {
    assert_eq!(wrapped().unwrap(), 19u8);
    let mut values = stream();
    assert_eq!(values.next().unwrap(), 23u8);
    assert!(values.next().is_none());
}
