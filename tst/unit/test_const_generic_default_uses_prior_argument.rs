struct Pair<const N: usize, const M: usize = N>([u8; N], [u8; M]);

fn main() {
    let _: Pair<13> = Pair(Default::default(), Default::default());
}
