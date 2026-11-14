//@ compile-only
//@ compile-flags: --emit=metadata

trait Pair {
    type First;
    type Second;
}

struct Both;

impl Pair for Both {
    type First = u8;
    type Second = u16;
}

trait One {}
impl<T: Pair<First = u8>> One for T {}

trait Two {}
impl<T: Pair<First = u8, Second = u16>> Two for T {}

fn require_one<T: One>() {}
fn require_two<T: Two>() {}
fn signature_one<T: Pair<First = u8>>(_: T::First) {}
fn signature_two<T: Pair<First = u8, Second = u16>>(_: T::First, _: T::Second) {}

fn main() {
    require_one::<Both>();
    require_two::<Both>();
    signature_one::<Both>(1u8);
    signature_two::<Both>(2u8, 3u16);
    let _: <Both as Pair>::First = 4u8;
    let _: <Both as Pair>::Second = 5u16;
}
