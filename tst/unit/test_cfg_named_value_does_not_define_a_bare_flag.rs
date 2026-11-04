//@ compile-flags: --cfg 'orchard_flavour="citrus"'
fn main() {
    assert!(cfg!(orchard_flavour = "citrus"));
    assert!(!cfg!(orchard_flavour));
}
