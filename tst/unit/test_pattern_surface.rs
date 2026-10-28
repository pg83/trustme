//@ crate-type: lib
#![allow(dead_code)]

enum E {
    A(i32),
    B { value: i32 },
    C,
}

fn patterns(value: E, values: &[i32], pair: (i32, i32)) -> i32 {
    let (left, right) = pair;
    let (first, second) = (left, right);
    let mut result = first + second;

    match value {
        E::A(x) if x > 0 => result += x,
        E::A(x) => result -= x,
        E::B { value: x } => result += x,
        E::C => result += 1,
    }

    match values {
        [head, tail @ ..] => result += head + tail.len() as i32,
        [] => result += 1,
    }

    match result {
        0..=10 => result += 1,
        11..=20 => result += 2,
        _ => result += 3,
    }

    let reference = &result;
    match reference {
        0 => 0,
        value => *value,
    }
}
