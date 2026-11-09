#![feature(const_closures, const_trait_impl)]

fn main() {
    const BASE: usize = 67;
    let callback = { const || 71usize };
    let value = { const { 73usize } };
    let shifted = {
        const OFFSET: usize = 3;
        OFFSET + BASE
    };
    assert_eq!((callback(), value, shifted), (71, 73, 70));
}
