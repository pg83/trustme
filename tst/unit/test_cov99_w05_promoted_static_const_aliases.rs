const fn same(value: usize) -> usize {
    value
}

struct Slot<const N: usize>;

impl<const N: usize> Slot<N> {
    const REF: &'static usize = &N;
}

fn main() {
    let first = Slot::<9>::REF;
    let second = Slot::<{ same(9) }>::REF;
    assert_eq!(*first, 9);
    assert_eq!(*second, 9);
}
