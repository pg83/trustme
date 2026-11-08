struct Pair(u8);
struct Mark;
union Bits { value: u8, bytes: [u8; 1] }
enum Entry { Empty, Tuple(u8), Named { value: u8 } }
type Alias = Entry;

fn main() {
    let pair = Pair(2);
    let indexed = Pair { 0: 3 };
    let mark = Mark;
    let braced_mark = Mark {};
    let tuple = Entry::Tuple(5);
    let named = Entry::Named { value: 7 };
    let empty = Alias::Empty {};
    let unit = Entry::Empty;
    let bits = Bits { value: 11 };
    let tuple_value = match tuple { Entry::Tuple(value) => value, _ => 0 };
    let named_value = match named { Entry::Named { value } => value, _ => 0 };
    assert_eq!(pair.0 + indexed.0 + unsafe { bits.value } + tuple_value + named_value, 28);
    assert!(matches!(empty, Entry::Empty));
    assert!(matches!(unit, Entry::Empty));
    let _ = (mark, braced_mark);
}
