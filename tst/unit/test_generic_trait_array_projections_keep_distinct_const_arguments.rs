trait Extract<const N: usize> {
    type Output;
    fn extract(self) -> Self::Output;
}

struct Packet<const N: usize>([u16; N]);

impl<const N: usize> Extract<N> for Packet<N> {
    type Output = [u16; N];

    fn extract(self) -> Self::Output {
        self.0
    }
}

fn extract<T: Extract<N>, const N: usize>(value: T) -> T::Output {
    value.extract()
}

fn main() {
    let first: [u16; 2] = extract::<Packet<2>, 2>(Packet([13, 29]));
    let second: [u16; 3] = extract::<Packet<3>, 3>(Packet([31, 37, 41]));
    assert_eq!(first, [13, 29]);
    assert_eq!(second, [31, 37, 41]);
    assert_eq!(extract::<Packet<2>, 2>(Packet([43, 47])), [43, 47]);
}
