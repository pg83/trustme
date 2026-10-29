//@ compile-flags: -Copt-level=2
use std::hint::black_box;
#[inline(never)]
fn unsigned(mode: u8, value: u8) -> u32 {
    if mode == 0 { match value { 0 => return 7, 3 => return 11, 19 => return 23, _ => return 31 } }
    else if mode == 1 { match value { 0 => return 7, 3 => return 11, 19 => return 23, _ => return 31 } }
    else { match value { 1 => return 7, 4 => return 11, 20 => return 23, _ => return 31 } }
}
#[inline(never)]
fn signed(mode: u8, value: i32) -> u32 {
    if mode == 0 { match value { -17 => return 5, -1 => return 13, 11 => return 29, _ => return 37 } }
    else if mode == 1 { match value { -17 => return 5, -1 => return 13, 11 => return 29, _ => return 37 } }
    else { match value { -18 => return 5, -2 => return 13, 10 => return 29, _ => return 37 } }
}
#[inline(never)]
fn text(mode: u8, value: &str) -> u32 {
    if mode == 0 { match value { "red" => return 2, "green" => return 3, "blue" => return 5, _ => return 7 } }
    else if mode == 1 { match value { "red" => return 2, "green" => return 3, "blue" => return 5, _ => return 7 } }
    else { match value { "tan" => return 2, "pink" => return 3, "black" => return 5, _ => return 7 } }
}
fn main() {
    for mode in 0..3 {
        for (value, same, different) in [(0, 7, 31), (3, 11, 31), (19, 23, 31), (1, 31, 7), (4, 31, 11), (20, 31, 23), (255, 31, 31)] {
            assert_eq!(unsigned(black_box(mode), black_box(value)), if mode == 2 { different } else { same });
        }
        for (value, same, different) in [(-17, 5, 37), (-1, 13, 37), (11, 29, 37), (-18, 37, 5), (-2, 37, 13), (10, 37, 29), (i32::MIN, 37, 37)] {
            assert_eq!(signed(black_box(mode), black_box(value)), if mode == 2 { different } else { same });
        }
        for (value, same, different) in [("red", 2, 7), ("green", 3, 7), ("blue", 5, 7), ("tan", 7, 2), ("pink", 7, 3), ("black", 7, 5), ("", 7, 7), ("blue!", 7, 7)] {
            assert_eq!(text(black_box(mode), black_box(value)), if mode == 2 { different } else { same });
        }
    }
}
