struct Mark {
    code: u8,
}

fn main() {
    let mut mark = Mark { code: 0 };
    assert!(if (mark = Mark { code: 9 }) == () {
        mark.code == 9
    } else {
        false
    });
    let mut sum = 1u8;
    assert!(if (sum += 4) == () { sum == 5 } else { false });
}
