//@ check-pass
//@ compile-flags: -Znext-solver

fn offset(index: usize) -> usize {
    index * 2 + 1
}

fn main() {
    assert_eq!(offset(3), 7);
}
