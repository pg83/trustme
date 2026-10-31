enum Unit { Only }
enum Tuple<T> { Only(T, T) }
enum Named<T> { Only { value: T, extra: u32 } }
fn main() {
    let unit = |Unit::Only| 2u32;
    assert_eq!(unit(Unit::Only), 2);
    let tuple = |Tuple::Only(left, right)| left + right;
    assert_eq!(tuple(Tuple::Only(3u16, 5)), 8);
    let named = |Named::Only { value, extra }| value + extra;
    assert_eq!(named(Named::Only { value: 7u32, extra: 11 }), 18);
}
