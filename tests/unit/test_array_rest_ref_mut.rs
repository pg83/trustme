fn main() {
    let mut values = [1, 2, 3, 4];

    match values {
        [head, ref mut tail @ ..] => {
            assert_eq!(head, 1);
            tail[0] = 20;
            tail[2] = 40;
        }
    }

    assert_eq!(values, [1, 20, 3, 40]);
}
