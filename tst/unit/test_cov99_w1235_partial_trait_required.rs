//@ compile-fail: Omitted type parameter with no default in

trait Required<A, B> {
    fn value();
}

impl Required<u8, u16> for () {
    fn value() {}
}

fn main() {
    <() as Required<u8>>::value();
}
