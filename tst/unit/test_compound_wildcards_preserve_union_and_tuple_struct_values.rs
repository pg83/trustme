struct Pair(u8, u8);
struct Record { pair: Pair, tail: u8 }
struct Unit;
union Bits<T: Copy> { value: T, empty: () }
fn main() {
    let first = match (Some(17), Record { pair: Pair(23, 37), tail: 41 }) {
        (Some(value), _) => value,
        _ => 0,
    };
    assert_eq!(first, 17);
    assert_eq!(match Pair(17, 23) { Pair { .. } => 29 }, 29);
    assert_eq!(match Unit { Unit => 31 }, 31);
    let bytes = *b"rust";
    assert!(matches!(&bytes, b"rust"));
    let [head, middle @ .., tail] = [3u8, 5, 7, 11];
    assert_eq!((head, middle, tail), (3, [5, 7], 11));
    let bits = Bits { value: 37u16 };
    let extracted = unsafe { match bits { Bits { value } => value } };
    assert_eq!(extracted, 37);
    assert_eq!(match (0, Bits { value: 41u16 }) { (0, _) => 43, _ => 0 }, 43);
}
