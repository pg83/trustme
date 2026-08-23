//@ compile-fail: trait bound
//@ crate-type: lib
// A plain generic trait argument is fully known here and must not turn a
// missing declaration bound into an inconclusive projection lookup.

trait Required<T> {}

struct Needs<T, U: Required<T>>(T, U);

fn bad<T, U>(_: Needs<T, U>) {}
