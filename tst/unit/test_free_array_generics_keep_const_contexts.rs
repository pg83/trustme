trait Seed { const VALUE: usize; }
impl Seed for u8 { const VALUE: usize = 3; }
impl Seed for u16 { const VALUE: usize = 11; }
fn make<T: Seed, const N: usize>() -> [usize; N] { std::array::from_fn(|i| T::VALUE + i) }
fn main() {
    let empty = make::<u8, 0>();
    let small = make::<u16, 5>();
    let wide = make::<u8, 33>();
    let later = make::<u16, 17>();
    assert_eq!(empty, []);
    assert_eq!(small, [11, 12, 13, 14, 15]);
    assert_eq!((wide[0], wide[32], wide.iter().sum::<usize>()), (3, 35, 627));
    assert_eq!((later[0], later[16], later.iter().sum::<usize>()), (11, 27, 323));
}
