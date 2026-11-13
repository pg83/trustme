//@ compile-fail: error:0:

struct Holder<const N: usize>;

impl<const N: usize> Holder<N> {
    const VALUE: u8 = match 0u8 {
        Holder::<3>::VALUE => 1u8,
        _ => 2u8,
    };
}

const ROOT: u8 = Holder::<3>::VALUE;

fn main() {
    let _ = ROOT;
}
