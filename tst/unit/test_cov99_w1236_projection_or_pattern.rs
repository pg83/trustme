//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Family<A> {
    type Item;
}

struct First;
struct Second;

impl<A> Family<A> for First {
    type Item = u8;
}

impl<A> Family<A> for Second {
    type Item = u16;
}

enum Choice<S: Family<u8> + Family<u16>> {
    First(<S as Family<u8>>::Item),
    Second(<S as Family<u16>>::Item),
}

fn make<S: Family<u8> + Family<u16>>() -> Choice<S> {
    loop {}
}

fn probe() {
    let value = make();
    match &value {
        Choice::First(x) | Choice::Second(x) => { let _ = x; }
    }
    let _: Choice<First> = value;
}
