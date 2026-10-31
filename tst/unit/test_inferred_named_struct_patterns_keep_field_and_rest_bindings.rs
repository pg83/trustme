struct Record<T> { first: T, second: T, extra: u32 }
fn main() {
    let sum = |Record { first, second, .. }| first + second;
    assert_eq!(sum(Record { first: 3u32, second: 5, extra: 7 }), 8);
    let rest = |Record { first, extra, .. }| first + extra;
    assert_eq!(rest(Record { first: 11u32, second: 13, extra: 17 }), 28);
}
