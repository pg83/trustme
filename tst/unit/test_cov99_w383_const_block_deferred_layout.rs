//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

struct Key<T>(T);
struct Receiver;
trait Route {
    type Target;
}

impl Route for Key<[u8; const {
    let byte = 3u8;
    let capture = || byte;
    std::mem::size_of_val(&capture)
}]> {
    type Target = Receiver;
}

impl Receiver {
    fn inspect(self: <Key<[u8; const {
        let byte = 3u8;
        let capture = || byte;
        std::mem::size_of_val(&capture)
    }]> as Route>::Target) {}
}
