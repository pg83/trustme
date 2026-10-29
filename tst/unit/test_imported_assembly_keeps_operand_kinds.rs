//@ aux-build: serialized_assembly.rs
use serialized_assembly::{choose, fold, stamp};
fn main() {
    assert_eq!(fold::<5>(11), 16);
    assert_eq!(fold::<0>(29), 29);
    assert_eq!(stamp(), 37);
    assert_eq!(choose::<13>(0), 20);
    assert_eq!(choose::<13>(4), 13);
}
