//@ compile-variant: -O
fn main() {
    let mut values = Vec::with_capacity(2);
    for value in 0..97u64 {
        values.push(value * 3 + 1);
    }
    values.reserve(200);
    let snapshot = values.clone().into_boxed_slice();
    values.clear();
    assert!(values.is_empty());
    assert_eq!(snapshot.len(), 97);
    assert_eq!(snapshot[0], 1);
    assert_eq!(snapshot[96], 289);
    let mut total = 0;
    for value in snapshot.iter() { total += *value; }
    assert_eq!(total, 14065);
}
