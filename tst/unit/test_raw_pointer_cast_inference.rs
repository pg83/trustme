fn main() {
    let values = [10_u32, 20, 30];
    let first: *const u32 = &values[0];

    assert_eq!(first, &values as *const _);
}
