fn main() {
    let value = const {
        let [first, ref middle @ .., last] = [13u16, 17, 19, 23];
        (first, middle.len(), last)
    };
    assert_eq!(value, (13, 2, 23));
}
