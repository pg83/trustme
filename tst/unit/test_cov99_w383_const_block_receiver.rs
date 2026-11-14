//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

struct Key<T>(T);
struct Receiver;
trait Route {
    type Target;
}

impl Route for Key<[u8; const { 1 + 2 }]> {
    type Target = Receiver;
}

impl Receiver {
    fn inspect(self: <Key<[u8; const { 1 + 2 }]> as Route>::Target) {}
}
