fn main() {
    let values: Vec<u8> = [1u8, 2]
        .into_iter()
        .filter_map(|value| Some(value))
        .filter_map(|value| Some(value))
        .chain(
            [3u8, 4]
                .into_iter()
                .filter_map(|value| Some(value))
                .filter_map(|value| Some(value)),
        )
        .collect();
    assert_eq!(values, [1, 2, 3, 4]);
}
