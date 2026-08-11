//@ check-pass
//@ compile-flags: -Znext-solver

fn sum_refs<'a, I>(iter: I) -> i8
where
    I: Iterator<Item = &'a i8>,
{
    iter.fold(0, |acc, value| acc + value)
}

fn main() {
    assert_eq!(sum_refs([1_i8, 2, 3].iter()), 6);
}
