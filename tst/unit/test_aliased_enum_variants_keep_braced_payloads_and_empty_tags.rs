enum Entry<T> { Tuple(T), Named { value: T } }
type Alias = Entry<u8>;
enum Flat { Only }
type FlatAlias = Flat;

fn main() {
    let named = Alias::Named { value: 17 };
    let tuple = Alias::Tuple { 0: 19 };
    let empty = FlatAlias::Only {};
    let direct = Flat::Only {};
    assert_eq!(match named { Entry::Named { value } => value, _ => 0 }, 17);
    assert_eq!(match tuple { Entry::Tuple(value) => value, _ => 0 }, 19);
    assert!(matches!(empty, Flat::Only));
    assert!(matches!(direct, Flat::Only));
}
