use std::hash::{Hash, Hasher};

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, PartialOrd, Ord, Hash)]
struct Unit;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, PartialOrd, Ord, Hash)]
struct Named<T> { first: T, second: u8 }

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, PartialOrd, Ord, Hash)]
struct Tuple<T>(T, u8);

#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
enum Choice<T> { Empty, Tuple(T, u8), Named { value: T } }

#[derive(Default, Debug, PartialEq)]
enum TupleDefault<T, U> { #[default] Value(T, u8), Unused(U) }

#[derive(Default, Debug, PartialEq)]
enum NamedDefault<T, U> { #[default] Value { first: T, second: u8 }, Unused(U) }

#[derive(Debug, PartialEq)]
struct NoDefault;

struct Bytes(Vec<u8>);

impl Hasher for Bytes {
    fn finish(&self) -> u64 { self.0.len() as u64 }
    fn write(&mut self, bytes: &[u8]) { self.0.extend_from_slice(bytes); }
}

fn hash<T: Hash>(value: &T) -> Vec<u8> {
    let mut bytes = Bytes(Vec::new());
    value.hash(&mut bytes);
    bytes.0
}

fn main() {
    let named = Named { first: 3u8, second: 7 };
    let tuple = Tuple(3u8, 7);
    assert_eq!(named.clone(), named);
    assert_eq!(tuple.clone(), tuple);
    assert_eq!(Unit.clone(), Unit::default());
    assert_eq!(Named::<u8>::default(), Named { first: 0, second: 0 });
    assert_eq!(Tuple::<u8>::default(), Tuple(0, 0));
    assert!(Named { first: 3u8, second: 6 } < named);
    assert!(Tuple(2u8, 9) < tuple);
    assert_eq!(Unit.cmp(&Unit), std::cmp::Ordering::Equal);
    assert_eq!(hash(&Unit), []);
    assert_eq!(hash(&named), [3, 7]);
    assert_eq!(hash(&tuple), [3, 7]);
    assert_eq!(format!("{:?}", named), "Named { first: 3, second: 7 }");
    assert_eq!(format!("{:?}", tuple), "Tuple(3, 7)");
    assert_eq!(format!("{:?}", Unit), "Unit");

    let empty = Choice::<u8>::Empty;
    let pair = Choice::Tuple(3u8, 7);
    let field = Choice::Named { value: 3u8 };
    assert_eq!(pair.clone(), pair);
    assert_eq!(field.clone(), field);
    assert!(empty < pair && pair < field);
    assert!(Choice::Tuple(3u8, 6) < pair);
    assert!(Choice::Named { value: 2u8 } < field);
    assert_ne!(hash(&empty), hash(&pair));
    assert_ne!(hash(&pair), hash(&field));
    assert_eq!(format!("{:?}", field), "Named { value: 3 }");
    assert_eq!(TupleDefault::<u8, NoDefault>::default(), TupleDefault::Value(0, 0));
    assert_eq!(NamedDefault::<u8, NoDefault>::default(), NamedDefault::Value { first: 0, second: 0 });
}
