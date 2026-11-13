struct Seed;

trait Twin<const A: usize, const B: usize> {
    fn arrays() -> ([u8; A], [u8; B]);
}

impl<const N: usize> Twin<N, N> for Seed {
    fn arrays() -> ([u8; N], [u8; N]) {
        ([1; N], [2; N])
    }
}

fn main() {
    let pair: ([u8; 2], [u8; 2]) = Seed::arrays();
    assert_eq!(pair.0, [1, 1]);
    assert_eq!(pair.1, [2, 2]);
}
