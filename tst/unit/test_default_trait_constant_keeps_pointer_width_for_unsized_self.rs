trait PointerWidth {
    const BYTES: usize = std::mem::size_of::<*const Self>();
}

impl PointerWidth for u8 {}
impl PointerWidth for str {}
impl PointerWidth for [u16] {}

trait Marker {}
impl PointerWidth for dyn Marker {}

fn main() {
    let word = std::mem::size_of::<usize>();
    assert_eq!(<u8 as PointerWidth>::BYTES, word);
    assert_eq!(<str as PointerWidth>::BYTES, 2 * word);
    assert_eq!(<[u16] as PointerWidth>::BYTES, 2 * word);
    assert_eq!(<dyn Marker as PointerWidth>::BYTES, 2 * word);
}
