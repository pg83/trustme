fn main() {
    let ((head, .., tail), [first, middle @ .., last]) =
        ((3u8, 5, 7, 11), [13u16, 17, 19, 23]);
    assert_eq!((head, tail, first, last), (3, 11, 13, 23));
    assert_eq!(middle, [17, 19]);
    let values: &[u16] = &[29, 31, 37];
    match values {
        [first, middle @ .., last] => {
            assert_eq!((*first, *last), (29, 37));
            assert_eq!(middle, &[31]);
        }
        _ => panic!("slice pattern lost its elements"),
    }
}
