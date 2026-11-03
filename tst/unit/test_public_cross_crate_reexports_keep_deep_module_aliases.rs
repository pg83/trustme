//@ aux-build: reexport_seed.rs
//@ aux-build: reexport_public_bridge.rs
extern crate reexport_public_bridge as bridge;
use bridge::facade::second::{Number, build};
use bridge::origin::doorway::second::Choice::Value;
use bridge::nested::values as local;
fn main() {
    let Number(value) = build(32);
    assert_eq!(value, 733);
    let bridge::nested::Choice::Value(selected) = Value(739);
    assert_eq!(selected, 739);
    assert_eq!(local::build(42).0, 743);
}
