//@ compile-only
//@ compile-flags: --emit=metadata

struct Pair {
    value: u8,
    spare: u8,
}

fn main() {
    let value: u8;
    Pair {
        #[macro_export]
        value,
        ..
    } = Pair { value: 7, spare: 9 };
    let _: u8 = value;
}
