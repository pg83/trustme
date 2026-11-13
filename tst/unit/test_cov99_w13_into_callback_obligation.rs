fn main() {
    let values: Vec<u8> = [1u8, 2]
        .into_iter()
        .filter_map(|value| value.into())
        .chain([3u8, 4].into_iter().filter_map(|value| value.into()))
        .collect();
    assert_eq!(values, [1, 2, 3, 4]);
}
