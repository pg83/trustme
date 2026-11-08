trait Family { type Item<const N: usize>; }
struct Seed;
impl Family for Seed { type Item<const N: usize> = [u8; N]; }

const fn extent<T: Family>() -> usize {
    std::mem::size_of::<T::Item<3>>()
}

trait Stamp { const TAG: u8; }
impl Stamp for [u8; 3] { const TAG: u8 = 19; }
const RESULT: u8 = <[u8; extent::<Seed>()]>::TAG;

fn main() {
    assert_eq!(RESULT, 19);
}
