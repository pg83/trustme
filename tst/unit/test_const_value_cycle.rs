//@ compile-fail: cycle detected
// Two constants whose initializers read each other must be reported as a
// cycle (rustc: E0391), not overflow the evaluator's stack.

const A: u8 = B;
const B: u8 = A;

fn main() {
    let _ = A;
}
