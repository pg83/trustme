fn main() {
    let values: Vec<u32> = [2u16, 5, 8]
        .into_iter()
        .filter_map(|value| {
            if value % 2 == 1 {
                Some(std::iter::once(value as u32))
            } else {
                None
            }
        })
        .flatten()
        .chain(
            [7u8, 4]
                .into_iter()
                .filter_map(|value| {
                    if value % 2 == 1 {
                        Some(std::iter::once(value as u32))
                    } else {
                        None
                    }
                })
                .flatten(),
        )
        .collect();
    assert_eq!(values, [5, 7]);
}
