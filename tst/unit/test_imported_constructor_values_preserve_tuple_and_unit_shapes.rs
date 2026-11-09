//@ aux-build: constructor_binding_shapes.rs
extern crate constructor_binding_shapes;

use constructor_binding_shapes::{Choice, Tuple as Pair, Unit as Marker};

fn main() {
    let constructor = Pair;
    assert_eq!(constructor(31).0, 31);
    let _marker = Marker;
    let variant = Choice::Tuple;
    assert!(matches!(variant(37), Choice::Tuple(37)));
    assert!(matches!(Choice::Unit, Choice::Unit));
    assert!(matches!(Choice::Named { value: 41 }, Choice::Named { value: 41 }));
}
