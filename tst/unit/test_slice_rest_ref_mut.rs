fn mutate_tail(values: &mut [i32]) {
    match *values {
        [ref mut head, ref mut tail @ ..] => {
            *head = 10;
            tail[0] = 20;
            tail[2] = 40;
        }
        [] => unreachable!(),
    }
}

fn main() {
    let mut values = [1, 2, 3, 4];
    mutate_tail(&mut values);

    assert_eq!(values, [10, 20, 3, 40]);
}
