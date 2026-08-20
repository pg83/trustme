//@ compile-fail: trait bound
//@ crate-type: lib
// A type is well formed only when the surrounding item supplies the bounds
// required by that type's declaration.

trait Required {}

struct Needs<T: Required>(T);

fn bad<T>(_: Needs<T>) {}
