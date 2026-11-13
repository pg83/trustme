fn shared(values: &[u16]) -> (u16, usize) {
    let read = || match values {
        [first, ref middle @ .., last] => (*first + *last, middle.len()),
        _ => (0, 0),
    };
    read()
}

fn unique(values: &mut [u16]) {
    let mut update = || match values {
        [first, ref mut rest @ ..] => {
            *first += 7;
            rest[0] += 11;
        }
        _ => (),
    };
    update();
}

fn main() {
    let mut values = [769u16, 773, 787, 797];
    assert_eq!(shared(&values), (1566, 2));
    unique(&mut values);
    assert_eq!(values, [776, 784, 787, 797]);

    let [first, middle @ .., last] = values;
    assert_eq!(middle, [784, 787]);
    assert_eq!(first + last, 1573);
}
