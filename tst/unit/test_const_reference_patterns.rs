#![allow(dead_code)]

const NUMBER: &i32 = &42;
const ZST: &() = unsafe { core::mem::transmute::<usize, &()>(10) };
const ARRAY: &[Option<()>; 1] = &[Some(())];
const SLICE: &[u16] = &[3, 5];
static VALUES: [i32; 3] = [7, 11, 13];
const MIDDLE: &i32 = &VALUES[1];

fn is_number(value: &i32) -> bool {
    match value {
        NUMBER => true,
        _ => false,
    }
}

fn is_zst(value: &()) -> bool {
    match value {
        ZST => true,
    }
}

fn is_middle(value: &i32) -> bool {
    match value {
        MIDDLE => true,
        _ => false,
    }
}

fn is_array(value: &[Option<()>; 1]) -> bool {
    match value {
        ARRAY => true,
        _ => false,
    }
}

fn is_slice(value: &[u16]) -> bool {
    match value {
        SLICE => true,
        _ => false,
    }
}

fn main() {
    assert!(is_number(&42));
    assert!(!is_number(&43));
    assert!(is_zst(&()));
    assert!(is_middle(&11));
    assert!(!is_middle(&7));
    assert!(is_array(&[Some(())]));
    assert!(!is_array(&[None]));
    assert!(is_slice(&[3, 5]));
    assert!(!is_slice(&[3, 4]));
    assert!(!is_slice(&[3, 5, 8]));
}
