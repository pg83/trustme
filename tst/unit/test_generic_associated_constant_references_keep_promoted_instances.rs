//@ compile-variant: -O
struct Tag<const N: usize>;
impl<const N: usize> Tag<N> {
    const DATA: [u8; 2] = [N as u8, (N + 1) as u8];
    const REFERENCE: &'static [u8; 2] = &Self::DATA;
}
fn main() {
    let first = Tag::<3>::REFERENCE;
    let second = Tag::<7>::REFERENCE;
    assert_eq!(*first, [3, 4]);
    assert_eq!(*second, [7, 8]);
    assert_eq!(*first, [3, 4]);
}
