#![feature(const_cmp, const_trait_impl)]

pub const fn classify(bytes: &[u8]) -> u8 {
    match bytes {
        b"" => 0,
        b"a\0\xff" => 1,
        b"ab" => 2,
        [0, rest @ ..] if rest.len() > 1 => 3,
        _ => 4,
    }
}

pub const fn fixed(bytes: &[u8; 3]) -> u8 {
    match bytes {
        b"a\0\xff" => 11,
        b"ab\0" => 12,
        _ => 13,
    }
}

pub fn carry<T: Copy>(value: T, bytes: &[u8]) -> (T, u8) {
    (value, classify(bytes))
}

pub const fn text(value: &str) -> u8 {
    match value {
        "" => 21,
        "a\0λ" => 22,
        "ab" => 23,
        _ => 24,
    }
}
