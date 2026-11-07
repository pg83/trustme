const RESULT: &(u16, usize, u16) = &{
    let [first, ref middle @ .., last] = [3u16, 5, 7, 11];
    (first, middle.len(), last)
};

fn main() {
    assert_eq!(*RESULT, (3, 2, 11));
}
