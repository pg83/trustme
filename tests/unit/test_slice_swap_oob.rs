//@ test-harness

#[test]
#[should_panic(expected = "index out of bounds: the len is 4 but the index is 4")]
fn raw_slice_index_oob_panics() {
    let mut values = ["a", "b", "c", "d"];
    let slice = &mut values[..];
    let _pointer = &raw mut slice[4];
}
