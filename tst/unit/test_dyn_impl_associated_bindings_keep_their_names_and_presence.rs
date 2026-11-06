//@ reject-flags: --cfg other_item => error || error
//@ reject-flags: --cfg no_bound => error || error
trait Shape {
    type Left where Self: Sized;
    type Right where Self: Sized;
}
trait Stamp { fn stamp() -> u8; }
impl Stamp for dyn Shape<Left = u8> {
    fn stamp() -> u8 { 31 }
}

#[cfg(other_item)]
fn rejected() { let _ = <dyn Shape<Right = u8> as Stamp>::stamp(); }
#[cfg(no_bound)]
fn rejected() { let _ = <dyn Shape as Stamp>::stamp(); }

fn main() {
    assert_eq!(<dyn Shape<Left = u8> as Stamp>::stamp(), 31);
}
