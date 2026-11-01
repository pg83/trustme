struct Record<T> { left: T, right: T }
struct Pair<T>(T, T);
enum Flag { Set, Clear }

fn main() {
    let record = |&Record { left, right }| left + right;
    assert_eq!(record(&Record { left: 11u16, right: 17 }), 28);
    let pair = |&Pair(left, right)| left * right;
    assert_eq!(pair(&Pair(3u32, 7)), 21);
    let flag = |(Flag::Set | Flag::Clear)| 31u8;
    assert_eq!(flag(Flag::Set), 31);
    assert_eq!(flag(Flag::Clear), 31);
}
