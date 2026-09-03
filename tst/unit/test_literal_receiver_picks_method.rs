// A method lookup on an integer or float literal receiver is what decides
// which primitive the literal becomes: the unique applicable candidate pins
// the literal to its own self type. Treating the literal as already closed
// makes that candidate inapplicable, the literal then takes its default, and
// the method is not found at all.

trait Marker {}

trait Blanket {
    fn tag(&self) -> u8 {
        1
    }
}

impl<F: Marker> Blanket for F {}

trait Direct {
    fn tag(&self) -> u8 {
        2
    }
}

impl Direct for f32 {}

struct S;

impl Marker for S {}

fn main() {
    assert_eq!(0.0.tag(), 2);
    assert_eq!(S.tag(), 1);
}
