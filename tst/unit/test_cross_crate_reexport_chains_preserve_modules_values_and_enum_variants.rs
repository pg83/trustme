//@ aux-build: reexport_seed.rs
//@ aux-build: reexport_bridge.rs
extern crate reexport_bridge;
use reexport_bridge::facade::second::{Number, build};
use reexport_bridge::origin::doorway::second::Choice::Value;
use reexport_bridge::nested::values as local;
fn main() {
    let Number(value) = build(8);
    assert_eq!(value, 709);
    let reexport_bridge::nested::Choice::Value(selected) = Value(719);
    assert_eq!(selected, 719);
    assert_eq!(local::build(26).0, 727);
}
