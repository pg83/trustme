//@ compile-flags: --cfg orchard_flavour
fn main() {
    assert!(cfg!(orchard_flavour));
    assert!(!cfg!(orchard_flavour = "citrus"));
}
