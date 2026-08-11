//@ check-pass

fn main() {
    let indexes = [0, 1];
    let values = [5, 6, 7, 8];
    let mut iter = indexes
        .iter()
        .map(|index| &values[*index..values.len()])
        .flatten();
    assert_eq!(iter.next_back(), Some(&8));
}
