#![feature(impl_trait_in_assoc_type)]

trait Capsule {
    type Value: Copy;
    const MARKER: u8;
    fn establish() where Self::Value:;
}

impl Capsule for () {
    type Value = impl Copy;
    const MARKER: u8 = 0;

    fn establish() where Self::Value: {
        let value: Self::Value = 41u32;
        let _ = value;
    }
}

struct Extent([u8; std::mem::size_of::<<() as Capsule>::Value>()]);

fn main() {
    let extent = Extent([0; 4]);
    assert_eq!(extent.0.len(), 4);
    assert_eq!(<() as Capsule>::MARKER, 0);
}
