struct Unit;
struct Pair<T>(T, T);
fn main() {
    let unit = |Unit| 7u32;
    assert_eq!(unit(Unit), 7);
    let pair = |Pair(left, right)| left + right;
    assert_eq!(pair(Pair(11u32, 13u32)), 24);
    let tail = |Pair(.., right)| right;
    assert_eq!(tail(Pair(17u32, 19u32)), 19);
}
