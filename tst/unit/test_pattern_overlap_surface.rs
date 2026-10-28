//@ crate-type: lib
#![allow(dead_code)]

enum E {
    A(i32),
    B(i32),
    C { x: i32, y: i32 },
}

fn overlap(e: E, pair: (i32, i32), slice: &[i32]) -> i32 {
    let mut result = 0;
    match e {
        E::A(x) if x > 0 => result += 1,
        E::A(x) if x < 0 => result += 2,
        E::A(_) => result += 3,
        E::B(x) => result += x,
        E::C { x, y } => result += x + y,
    }
    match pair {
        (0, 0) => result += 1,
        (0, _) => result += 2,
        (_, 0) => result += 3,
        _ => result += 4,
    }
    match slice {
        [1, 2, 3] => result += 1,
        [1, ..] => result += 2,
        [.., 3] => result += 3,
        _ => result += 4,
    }
    result
}
