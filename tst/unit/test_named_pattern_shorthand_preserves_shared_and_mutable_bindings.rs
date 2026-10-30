struct Pair { left: u16, right: u16 }
fn main() {
    let mut value = Pair { left: 3, right: 5 };
    let Pair { ref left, ref mut right } = value;
    *right += *left;
    let Pair { ref left, ref right } = value;
    assert_eq!((*left, *right), (3, 8));
    assert_eq!((value.left, value.right), (3, 8));
}
