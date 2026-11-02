//@ aux-build: promoted_generic_parameter_paths.rs
//@ compile-variant: -O
use promoted_generic_parameter_paths::RefTag;
fn main() {
    let first = RefTag::<5>::REFERENCE;
    let second = RefTag::<11>::REFERENCE;
    assert_eq!((*first, *second), ([5, 6], [11, 12]));
    assert_eq!(*first, [5, 6]);
}
