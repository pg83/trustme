#![feature(c_variadic)]

unsafe extern "C" fn inspect(_anchor: i32, mut arguments: ...) -> i32 {
    let head = arguments.arg::<i32>();
    let copied = arguments.with_copy(|mut copy| {
        let first = copy.arg::<i32>();
        let second = copy.arg::<i32>();
        (first, second)
    });
    let first = arguments.arg::<i32>();
    let second = arguments.arg::<i32>();
    assert_eq!(copied, (first, second));
    head + first + second
}

fn main() {
    assert_eq!(unsafe { inspect(0, 13i32, -5i32, 31i32) }, 39);
}
