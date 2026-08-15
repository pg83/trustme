fn main() {
    let mut source = &1;
    let destination = &mut &2;
    let replacement = &3;

    *{
        source = replacement;
        &mut *destination
    } = source;

    assert_eq!(**destination, 1);
    assert_eq!(*source, 3);
}
