const fn paired_width<const N: usize>(values: [u8; N]) -> usize {
    let aliases = (&values, &values);
    aliases.0.len() + aliases.1.len()
}

fn promoted<const N: usize>() -> (&'static [u8; N], usize, u8) {
    let first = &[7u8; N];
    let second = &[13u8; N];
    let aliases = (first, first);
    let measure = || aliases.0.len() + aliases.1.len() + second.len();
    let read = |index: usize| second[index];
    (aliases.0, measure(), read(N - 1))
}

fn main() {
    const WIDTH: usize = paired_width([1u8, 2, 3, 4]);
    assert_eq!(WIDTH, 8);
    let (short, short_width, short_last) = promoted::<2>();
    let (long, long_width, long_last) = promoted::<5>();
    assert_eq!(*short, [7u8; 2]);
    assert_eq!(*long, [7u8; 5]);
    assert_eq!((short_width, short_last), (6, 13));
    assert_eq!((long_width, long_last), (15, 13));
}
