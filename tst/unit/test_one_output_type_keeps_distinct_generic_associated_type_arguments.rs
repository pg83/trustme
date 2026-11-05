trait Convert {
    type Output<T>;
    fn convert<T>(&self, value: T) -> Self::Output<T>;
}

struct Counter(u32);
impl Convert for Counter {
    type Output<T> = u32;
    fn convert<T>(&self, _: T) -> u32 { self.0 }
}

fn both<C, R>(value: &C) -> (R, R)
where C: Convert<Output<u8> = R> + Convert<Output<u16> = R> {
    (value.convert(1u8), value.convert(2u16))
}

fn main() {
    let result: (u32, u32) = both(&Counter(19));
    assert_eq!(result, (19, 19));
}
