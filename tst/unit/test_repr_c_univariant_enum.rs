//@ run-pass
// A `repr(C)` or `repr(iN)` enum keeps its tag beside the data rather than in
// it, and that holds for a single-variant enum with a payload too: the data
// field is the only one, and the tag sits at its own offset.

pub enum Void {}

#[repr(C)]
enum UninhabitedC {
    #[allow(dead_code)]
    Variant(Void),
}

#[repr(i32)]
enum Uninhabited32 {
    #[allow(dead_code)]
    Variant(Void),
}

#[repr(C)]
enum OneVariantC {
    Variant(u16),
}

fn main() {
    let one = OneVariantC::Variant(7);
    match one {
        OneVariantC::Variant(v) => assert_eq!(v, 7),
    }

    // Only the types have to lay out and codegen; nothing inhabits these.
    let _: fn() -> Option<UninhabitedC> = || None;
    let _: fn() -> Option<Uninhabited32> = || None;
}
