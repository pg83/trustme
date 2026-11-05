//@ compile-variant: -O
#![feature(auto_traits)]

auto trait Portable {}
enum Parcel { Empty, Number(u16) }
trait Source { type Item; }
struct Provider;
impl Source for Provider { type Item = (u8, Parcel, [u16; 2]); }
fn require<T: Portable>(value: T) -> T { value }
fn inspect<T: Source>(value: T::Item) -> T::Item where T::Item: Portable {
    require(value)
}

fn main() {
    let (_, parcel, values) = inspect::<Provider>((7, Parcel::Number(19), [23, 29]));
    assert!(matches!(parcel, Parcel::Number(19)));
    assert_eq!(values[0] + values[1], 52);
    assert!(matches!(require(Parcel::Empty), Parcel::Empty));
}
