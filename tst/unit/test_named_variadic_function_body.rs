#![feature(c_variadic)]
unsafe extern "C" fn sum(count: u32, mut values: ...) -> i32 {
    let mut total = 0;
    for _ in 0..count {
        total += values.arg::<i32>();
    }
    total
}
unsafe extern "C" fn mixed(_anchor: u32, mut values: ...) -> f64 {
    let integer = values.arg::<i32>();
    let float = values.arg::<f64>();
    integer as f64 + float
}
fn main() {
    unsafe {
        assert_eq!(sum(0), 0);
        assert_eq!(sum(3, 7i32, -4i32, 11i32), 14);
        assert_eq!(mixed(0u32, 5i32, 2.5f64), 7.5);
    }
}
