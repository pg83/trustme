fn main() {
    let mut values: Box<[u8]> = vec![7, 11, 13].into_boxed_slice();
    {
        let ref whole = *values;
        let [ref all @ ..] = *values;
        assert_eq!(whole, all);
        assert_eq!(all.len(), 3);
    }
    {
        let ref mut whole = *values;
        whole[1] = 19;
    }
    let [first, second, third] = [values[0], values[1], values[2]];
    assert_eq!((first, second, third), (7, 19, 13));
}
