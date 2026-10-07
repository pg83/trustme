//@ proc-macro-aux-build: remember_recall.rs
//@ proc-macro-aux-build: dollar_crate_probe.rs
// tor-proto (arti) puts `#[enum_dispatch]` on an enum and
// `#[enum_dispatch(StreamFlowCtrlInner)]` on a trait; the second invocation
// finds the enum the first one stored in the proc macro's static cache and
// writes the trait impl for it. Upstream loads a proc macro crate once per
// compilation and runs every invocation in it, so its statics live across
// invocations. We started a new process per invocation, the cache was empty,
// no impl came out, and the calls found "No applicable methods".
// With two proc macro crates each kept running, one must not hold the
// other's pipes open, or the other never sees its input end.
use dollar_crate_probe::echo;
use remember_recall::{recall, remember};

#[remember]
#[allow(dead_code)]
struct Marker;

fn main() {
    assert!(recall!().contains("Marker"), "{}", recall!());
    assert_eq!(echo!(7), 7);
}
