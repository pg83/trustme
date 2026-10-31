#[derive(Clone, Debug, PartialEq)]
struct Named {
    #[cfg_attr(all(), cfg(all()), doc = "kept")]
    first: u16,
    #[cfg_attr(any(), cfg(any()))]
    last: u32,
}
#[derive(Clone, Debug, PartialEq)]
struct Tuple(#[cfg(all())] u16, #[cfg_attr(all(), cfg_attr(all(), cfg(all())))] u32);
enum Choice {
    #[cfg(all())]
    Unit,
    #[cfg_attr(all(), cfg(all()))]
    Tuple(#[cfg(all())] u16),
    Named { #[cfg(all())] value: u32 },
}
union Bits { #[cfg(all())] word: u32 }
fn main() {
    let named = Named { first: 3, last: 5 };
    assert_eq!(named.clone(), named);
    assert_eq!(Tuple(7, 11).clone(), Tuple(7, 11));
    let number = match (Choice::Named { value: 13 }) { Choice::Named { value } => value, _ => 0 };
    assert_eq!(number, 13);
    let Choice::Tuple(value) = Choice::Tuple(17) else { panic!() };
    assert_eq!(value, 17);
    assert!(matches!(Choice::Unit, Choice::Unit));
    assert_eq!(unsafe { Bits { word: 19 }.word }, 19);
}
