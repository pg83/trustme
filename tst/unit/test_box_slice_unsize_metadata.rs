fn main() {
    let array = Box::new([11isize, 22, 33]);
    let slice: Box<[isize]> = array;

    assert_eq!(slice.len(), 3);
    assert_eq!(slice[1], 22);
}
