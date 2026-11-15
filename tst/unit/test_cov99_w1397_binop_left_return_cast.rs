#![allow(unreachable_code)]

fn left_return() -> i32 {
    ((return 41) as i32) + 1i32
}

fn main() {
    assert_eq!(left_return(), 41);
}
