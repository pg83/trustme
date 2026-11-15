trait Length<const N: usize> {
    fn length() -> usize;
}

struct Buffer<const N: usize>;

impl<const N: usize> Length<N> for Buffer<N> {
    fn length() -> usize {
        N
    }
}

fn forwarded<const N: usize>() -> usize {
    Buffer::<N>::length()
}

fn main() {
    assert_eq!(forwarded::<7>(), 7);
}
