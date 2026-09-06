// Calling `Arc<Box<closure>>` resolves `<Box<closure> as FnOnce<()>>::Output`
// through Box's forwarding impl to `<closure as FnOnce<()>>::Output`, whose
// value is the closure's return variable - the very variable the call's result
// was unified with.  Upstream's occurs check walks the closure's signature
// (`ClosureArgs` are part of the closure type), so the variable is never bound
// to a projection over itself; the projection normalizes to the variable and
// the equality is trivial.

use std::sync::Arc;

fn main() {
    let x = 5;
    let command = Arc::new(Box::new(|| { x*2 }));
    assert_eq!(command(), 10);
}
