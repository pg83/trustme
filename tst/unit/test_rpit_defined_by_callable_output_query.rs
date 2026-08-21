//@ crate-type: lib

fn require_u8(_: impl Fn() -> u8) {}

fn define() -> impl Sized {
    require_u8(define);
    0u8
}
