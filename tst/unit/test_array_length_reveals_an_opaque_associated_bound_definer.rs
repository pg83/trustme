#![feature(impl_trait_in_assoc_type)]

trait Source {
    type Item: Copy;
    fn define<T: AsRef<Self::Item>>(_: T);
}
struct Owner;
impl Source for Owner {
    type Item = impl Copy;
    fn define<T: AsRef<Self::Item>>(_: T) {
        let value: Self::Item = 29u16;
        let _ = value;
    }
}
struct Bytes([u8; std::mem::size_of::<<Owner as Source>::Item>()]);

fn main() {
    let bytes = Bytes([7, 11]);
    assert_eq!(bytes.0, [7, 11]);
}
