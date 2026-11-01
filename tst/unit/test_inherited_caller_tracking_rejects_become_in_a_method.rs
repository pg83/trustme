//@ compile-fail: a function marked with `#[track_caller]` cannot perform a tail-call
#![feature(explicit_tail_calls)]
fn finish(value: u16) -> u16 { value + 3 }
trait Relay {
    #[track_caller]
    fn relay(value: u16) -> u16;
}
struct Worker;
impl Relay for Worker {
    fn relay(value: u16) -> u16 { become finish(value) }
}
fn main() { let _ = Worker::relay(7); }
