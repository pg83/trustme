// A built-in attribute imported under another name still runs.
use derive as my_derive;

#[my_derive(Debug, Clone, PartialEq)]
struct S(u32);

fn main() {
    let s = S(3);
    assert_eq!(format!("{:?}", s.clone()), "S(3)");
    assert_eq!(s, S(3));
}
