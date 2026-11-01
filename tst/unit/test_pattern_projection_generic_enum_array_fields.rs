enum Record<T, const N: usize> {
    Empty,
    Values { tag: u8, pair: (T, [u16; N]) },
    Tail(T),
}

fn select<T: Copy>(record: Record<T, 3>) -> Option<(T, u16)> {
    match record {
        Record::Values { tag: 3..=7, pair: (value, [first, .., last]) } => Some((value, first + last)),
        Record::Tail(value) => Some((value, 0)),
        Record::Empty | Record::Values { .. } => None,
    }
}

fn main() {
    assert_eq!(select(std::hint::black_box(Record::Values { tag: 5, pair: (17u8, [3, 7, 11]) })), Some((17, 14)));
    assert_eq!(select(std::hint::black_box(Record::Values { tag: 9, pair: (23u64, [1, 2, 3]) })), None);
    assert_eq!(select(std::hint::black_box(Record::Tail(37u16))), Some((37, 0)));
    assert_eq!(select::<u32>(Record::Empty), None);
}
