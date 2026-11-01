struct Record<T> { first: u16, payload: T, last: u16 }

fn select(value: Record<(u16, u16)>) -> u16 {
    match value {
        Record { payload: (3, tail), last: 5, .. } => tail,
        Record { first: 7, .. } => 11,
        whole @ _ => whole.payload.0 + whole.last,
    }
}

fn main() {
    assert_eq!(select(Record { first: 13, payload: (3, 17), last: 5 }), 17);
    assert_eq!(select(Record { first: 7, payload: (19, 23), last: 29 }), 11);
    assert_eq!(select(Record { first: 31, payload: (37, 41), last: 43 }), 80);
}
