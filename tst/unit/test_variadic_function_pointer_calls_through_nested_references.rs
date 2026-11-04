//@ compile-variant: -O
#![feature(c_variadic)]
unsafe extern "C" fn weighted(count: u32, mut arguments: ...) -> i64 {
    let mut result = 0;
    for index in 0..count {
        result += (index as i64 + 1) * arguments.arg::<i64>();
    }
    result
}
fn main() {
    let pointer: unsafe extern "C" fn(u32, ...) -> i64 = weighted;
    let borrowed = &&pointer;
    unsafe {
        assert_eq!(borrowed(0), 0);
        assert_eq!(borrowed(3, 7i64, -11i64, 13i64), 24);
        assert_eq!(pointer(2, -17i64, 19i64), 21);
    }
}
