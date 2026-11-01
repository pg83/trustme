//@ aux-build: const_dyn_pointer_transforms.rs
extern crate const_dyn_pointer_transforms as source;
const SMALL: &dyn source::Measure<u8> = source::choose_small(&source::NUMBER);
const LARGE: &dyn source::Measure<u32> = source::choose_large(&source::NUMBER);
fn main() {
    assert_eq!(SMALL.measure(11), 382);
    assert_eq!(LARGE.measure(29), 1400);
}
