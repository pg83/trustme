struct Owner<const VALUE: u8>;

impl<const VALUE: u8> Owner<VALUE> {
    const fn values() -> &'static [u8] {
        &[VALUE; 2]
    }
}

const FIRST: &[u8] = Owner::<5>::values();
const SECOND: &[u8] = Owner::<9>::values();

fn main() {
    assert_eq!(FIRST, [5; 2]);
    assert_eq!(SECOND, [9; 2]);
}
