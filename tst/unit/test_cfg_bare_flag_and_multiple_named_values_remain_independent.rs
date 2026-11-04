//@ compile-flags: --cfg orchard_flavour --cfg 'orchard_flavour="citrus"' --cfg 'orchard_flavour="stone"'
fn main() {
    assert!(cfg!(orchard_flavour));
    assert!(cfg!(orchard_flavour = "citrus"));
    assert!(cfg!(orchard_flavour = "stone"));
    assert!(!cfg!(orchard_flavour = "seed"));
}
