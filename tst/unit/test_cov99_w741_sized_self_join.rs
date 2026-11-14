fn width<T: Sized>(_: &T) -> usize {
    core::mem::size_of::<T>()
}

fn main() {
    let byte = 7u8;
    assert_eq!(width(if true { &byte } else { &&byte }), 1);
}
