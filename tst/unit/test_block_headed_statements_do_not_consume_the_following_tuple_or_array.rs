fn main() {
    let mut total = 0u16;

    if true {
        total += 1;
    }
    ({ total += 2; total },);
    assert_eq!(total, 3);

    loop {
        total += 4;
        break;
    }
    [{ total += 8; total }];
    assert_eq!(total, 15);
}
