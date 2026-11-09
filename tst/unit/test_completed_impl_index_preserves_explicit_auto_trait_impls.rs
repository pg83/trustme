#![feature(auto_traits, negative_impls)]

auto trait Portable {}

struct Parcel(u16);
impl Portable for Parcel {}

struct Stationary;
impl !Portable for Stationary {}

fn deliver<T: Portable>(value: T) -> T {
    value
}

fn main() {
    assert_eq!(deliver(Parcel(47)).0, 47);
}
