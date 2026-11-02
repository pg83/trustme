//@ aux-build: exported_generic_parameter_paths.rs
//@ compile-variant: -O
extern crate exported_generic_parameter_paths as source;
use source::{MakePoint, MakeVoid, Reexported, VALUE};
const POINTER: &u32 = &VALUE;
fn main() {
    let point = MakePoint::<u16>(47);
    assert_eq!(match point { source::Shape::Point(n) => n, source::Shape::Void => 0 }, 47);
    assert!(matches!(MakeVoid::<u32>, source::Shape::Void));
    let packet: Reexported<{ 2 + 3 }> = Reexported([2, 3, 5, 7, 11]);
    assert_eq!(packet.0[4], 11);
    assert_eq!(*POINTER, 53);
}
