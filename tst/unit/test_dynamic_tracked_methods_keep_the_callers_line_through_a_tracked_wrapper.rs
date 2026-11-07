use std::panic::Location;

trait Reporter {
    #[track_caller]
    fn report(&self, stamp: u16) -> (u32, u16);
}

struct Source;

impl Reporter for Source {
    fn report(&self, stamp: u16) -> (u32, u16) {
        (Location::caller().line(), stamp)
    }
}

#[inline(never)]
#[track_caller]
fn routed(target: &dyn Reporter, stamp: u16) -> (u32, u16) {
    target.report(stamp)
}

fn main() {
    let value = Source;
    let target = std::hint::black_box(&value as &dyn Reporter);
    let expected = line!() + 1;
    let result = routed(target, 61);
    assert_eq!(result, (expected, 61));
    let expected = line!() + 1;
    let result = target.report(67);
    assert_eq!(result, (expected, 67));
}
