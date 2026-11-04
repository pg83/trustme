//@ compile-variant: -O

const fn twice<const N: usize>() -> usize { N + N }
const fn signed<const N: i16>() -> i16 { N - 3 }
const fn select<const FLAG: bool>() -> usize {
    if FLAG { twice::<7>() } else { twice::<11>() }
}
const fn repeated<const N: usize>() -> usize { [17u8; N].len() }

struct Count<const N: usize>;
impl<const N: usize> Count<N> {
    const fn value() -> usize { N + 1 }
}

const TOTAL: usize = select::<true>() + Count::<5>::value();
const OTHER: usize = select::<false>();
const NEGATIVE: i16 = signed::<{ -7 }>();
const LENGTH: usize = repeated::<3>();

fn main() {
    assert_eq!(TOTAL, 20);
    assert_eq!(OTHER, 22);
    assert_eq!(NEGATIVE, -10);
    assert_eq!(LENGTH, 3);
}
