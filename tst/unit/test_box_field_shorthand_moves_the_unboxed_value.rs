#![feature(box_patterns)]

struct Parcel {
    value: Box<u16>,
}

fn main() {
    let Parcel { box value } = Parcel { value: Box::new(31) };
    assert_eq!(value, 31u16);
}
