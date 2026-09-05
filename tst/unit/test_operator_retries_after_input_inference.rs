// An operator goal that comes back ambiguous is put to sleep until one of the
// types it was checked against changes.  Checking it can itself refine those
// types - `bytes == &[]` names the array's element while leaving the goal
// unresolved - so the sleep has to watch the inputs as they were when the check
// ran.  Watching them as the check left them made that refinement invisible and
// the rule never woke, leaving the element type unnamed.

fn main() {
    let bytes: &[u8] = &[];
    assert!(bytes == &[]);
    assert_eq!(bytes, &[]);

    let words: &[u32] = &[];
    assert_eq!(words, &[]);

    let numbers: &[u8] = &[1, 2];
    assert_eq!(numbers, &[1, 2]);
}
