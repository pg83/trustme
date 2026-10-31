//@ test-harness
//@ edition: 2015
//@ compile-flags: -Z fmt-debug=shallow -O -g -l c -lc -Adead_code
use std::cell::Cell;
use std::fmt;
struct Probe<'a>(&'a Cell<u8>);
impl fmt::Debug for Probe<'_> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.0.set(self.0.get() + 1);
        formatter.write_str("visited")
    }
}
#[derive(Debug)]
struct Envelope<'a> { value: Probe<'a> }
#[derive(Debug)]
enum Packet<'a> { Data(Probe<'a>), Empty }
#[test]
fn derived_formatting_uses_only_the_outer_name() {
    let calls = Cell::new(0);
    assert_eq!(format!("{:?}", Envelope { value: Probe(&calls) }), "Envelope");
    assert_eq!(format!("{:?}", Packet::Data(Probe(&calls))), "Data");
    assert_eq!(format!("{:?}", Packet::Empty), "Empty");
    assert_eq!(calls.get(), 0);
}
#[test]
fn handwritten_debug_still_runs() {
    let calls = Cell::new(0);
    assert_eq!(format!("{:?}", Probe(&calls)), "visited");
    assert_eq!(calls.get(), 1);
}
