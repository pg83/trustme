//@ compile-fail: cycle detected
// Two statics whose initializers read each other's value must be reported
// as a cycle (rustc: E0391). Taking a static's address while it is being
// evaluated stays legal; reading its bytes is the cycle.

static A: u8 = B;
static B: u8 = A;

fn main() {
    let _ = A;
}
