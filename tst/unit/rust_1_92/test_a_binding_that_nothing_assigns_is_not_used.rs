//@ compile-fail: used binding isn't initialized
// A closure's `let _ = x;` still reads `x`, so `x` has to be initialized:
// rustc's borrow checker reports E0381 "used binding `x` isn't initialized"
// (the Rust Reference's types/closure.md example). We have no borrow
// checker, but a local that is read and that nothing assigns anywhere is
// such a use too; the MIR's garbage collection asserted on it.
fn main() {
    let x: u8;
    let c = || {
        let _ = x;
    };
    c();
}
