const fn generated<const N: usize>() -> usize {
    const { N * 3 + 1 }
}

const fn nested() -> usize {
    let left = const { generated::<5>() };
    left + generated::<7>()
}

const TOTAL: usize = nested();

fn main() {
    let value: &'static usize = &const { generated::<11>() };
    assert_eq!(TOTAL, 38);
    assert_eq!(*value, 34);
}
