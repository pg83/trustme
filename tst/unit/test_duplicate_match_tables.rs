#[inline(never)]
fn unsigned(flag: bool, value: u32) -> u8 {
    if flag {
        match value { 2 => 10, 7 => 20, 15 => 30, _ => 40 }
    } else {
        match value { 2 => 10, 7 => 20, 15 => 30, _ => 40 }
    }
}

#[inline(never)]
fn signed(flag: bool, value: i32) -> u8 {
    if flag {
        match value { -9 => 10, -3 => 20, 5 => 30, _ => 40 }
    } else {
        match value { -9 => 10, -3 => 20, 5 => 30, _ => 40 }
    }
}

#[inline(never)]
fn strings(flag: bool, value: &str) -> u8 {
    if flag {
        match value { "red" => 10, "green" => 20, "blue" => 30, _ => 40 }
    } else {
        match value { "red" => 10, "green" => 20, "blue" => 30, _ => 40 }
    }
}

#[inline(never)]
fn bytes(flag: bool, value: &[u8]) -> u8 {
    if flag {
        match value { b"red" => 10, b"green" => 20, b"blue" => 30, _ => 40 }
    } else {
        match value { b"red" => 10, b"green" => 20, b"blue" => 30, _ => 40 }
    }
}

fn main() {
    for flag in [false, true] {
        let flag = std::hint::black_box(flag);
        for (value, expected) in [(2, 10), (7, 20), (15, 30), (1, 40)] {
            assert_eq!(unsigned(flag, value), expected);
        }
        for (value, expected) in [(-9, 10), (-3, 20), (5, 30), (0, 40)] {
            assert_eq!(signed(flag, value), expected);
        }
        for (value, expected) in [("red", 10), ("green", 20), ("blue", 30), ("gray", 40)] {
            assert_eq!(strings(flag, value), expected);
            assert_eq!(bytes(flag, value.as_bytes()), expected);
        }
    }
}
