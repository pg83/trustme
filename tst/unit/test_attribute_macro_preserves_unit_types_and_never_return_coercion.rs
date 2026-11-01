//@ proc-macro-aux-build: preserve_bound_types.rs
use preserve_bound_types::preserve;

#[preserve]
type Units = ((), (u8, ()));

#[preserve]
fn unit(value: ()) -> () { value }

#[preserve]
fn never() -> ! { loop {} }

fn choose(function: fn() -> !, stop: bool) -> u16 { if stop { function() } else { 17 } }

fn main() {
    let value: Units = (unit(()), (7, unit(())));
    assert_eq!(value.1.0, 7);
    assert_eq!(choose(never, false), 17);
}
