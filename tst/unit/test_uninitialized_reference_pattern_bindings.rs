fn main() {
    let &(first, last): &(u8, u16);
    last = 257;
    first = 19;
    assert_eq!((first, last), (19, 257));
    let &mut (mut left, right): &mut (u16, u16);
    left = 23;
    right = 29;
    left += right;
    assert_eq!(left, 52);
}
