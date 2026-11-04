//@ compile-flags: --cfg 'orchard_flavour="citrus"'
macro_rules! selected { ($value:expr) => { cfg!(orchard_flavour = $value) }; }
fn main() {
    assert!(selected!("citrus"));
    assert!(!selected!("stone"));
    assert!(!selected!("citrus "));
}
