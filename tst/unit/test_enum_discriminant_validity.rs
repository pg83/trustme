//@ compile-fail: assigned more than once
// Two variants cannot share a discriminant, and a discriminant has to fit the
// enum's tag type. Both were accepted silently, which let the corpus build
// enums whose variants are indistinguishable.
//
// Same shapes as the Rust Reference examples items/enumerations.md:169 and
// :185 (the second is `#[repr(u8)] enum { Max = 255, MaxPlusOne }`).
enum Shared {
    A = 1,
    B = 1,
}

fn main() {
    let _ = Shared::A;
}
