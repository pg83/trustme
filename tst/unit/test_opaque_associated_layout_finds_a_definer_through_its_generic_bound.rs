#![feature(impl_trait_in_assoc_type)]

trait Factory {
    type Hidden: Copy;
    fn establish<T: AsRef<Self::Hidden>>(_: T);
}
struct Maker;
impl Factory for Maker {
    type Hidden = impl Copy;
    fn establish<T: AsRef<Self::Hidden>>(_: T) {
        let value: Self::Hidden = 37u8;
        let _ = value;
    }
}

fn main() {
    assert_eq!(std::mem::size_of::<<Maker as Factory>::Hidden>(), 1);
}
