// datafrog 2.0's `ExtendWith::intersect` (polonius-engine builds it):
//     values.retain(|v| { slice = gallop(slice, |kv| &kv.1 < v); .. })
// compares `&Val` with `v: &&Val`. Upstream `check_overloaded_binop` looks the
// operator method up with the right-hand type a fresh `?R`, then coerces the
// right operand into `?R`; the coercion first structurally resolves `?R`
// (`try_structurally_resolve_type`, which runs `select_where_possible`), and
// the one impl that can hold for `&Val: PartialOrd<?R>`, `PartialOrd<&B> for
// &A`, makes it `&?B` - so the operand is dereferenced into `&Val`, and
// `?B = Val` by `Val: Ord`. Here the operand's own type was taken for `?R` up
// front, and `&Val: PartialOrd<&&Val>` has no impl.
fn gallop<T>(mut slice: &[T], mut cmp: impl FnMut(&T) -> bool) -> &[T] {
    while !slice.is_empty() && cmp(&slice[0]) {
        slice = &slice[1..];
    }
    slice
}

fn intersect<Val: Ord>(relation: &[(u32, Val)], values: &mut Vec<&Val>) {
    let mut slice = relation;
    values.retain(|v| {
        slice = gallop(slice, |kv| &kv.1 < v);
        slice.get(0).map(|kv| &kv.1) == Some(v)
    });
}

fn main() {
    let relation = [(0, 1), (0, 3), (0, 5)];
    let (one, two, three) = (1, 2, 3);
    let mut values = vec![&one, &two, &three];
    intersect(&relation, &mut values);
    assert_eq!(values, vec![&1, &3]);
}
