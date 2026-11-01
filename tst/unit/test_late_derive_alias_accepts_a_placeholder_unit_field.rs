#![feature(trace_macros)]
macro_rules! install { () => { use core::clone::Clone as LateClone; }; }
#[derive(LateClone)]
struct Placeholder(trace_macros!(false));
install!();
fn main() { let original = Placeholder(()); let cloned = original.clone(); let Placeholder(()) = cloned; }
