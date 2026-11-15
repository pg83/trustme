//@ compile-fail: Raising temporary across a freeze?
//@ compile-flags: --emit=metadata -Cpanic=abort

fn make() -> u8 {
    1
}

fn accept(_: ()) -> bool {
    true
}

fn main() {
    match 0u8 {
        0 | 1 if accept(match () { () => let ref borrowed = make() }) => (),
        _ => (),
    }
}
