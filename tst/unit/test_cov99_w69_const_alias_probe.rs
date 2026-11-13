struct Buffer<const N: usize>([u8; N]);

type Alias<const N: usize> = Buffer<N>;

impl<const N: usize> Buffer<N> {
    fn new(bytes: [u8; N]) -> Self {
        Self(bytes)
    }
}

fn main() {
    let value = Alias::<_>::new([9u8; 4]);
    assert_eq!(value.0, [9u8; 4]);
}
