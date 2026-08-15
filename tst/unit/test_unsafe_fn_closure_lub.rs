unsafe fn add(left: i32, right: i32) -> i32 {
    left + right
}

fn main() {
    let function_first = if true {
        add
    } else {
        |left, right| left - right
    };
    assert_eq!(unsafe { function_first(4, 5) }, 9);

    let closure_first = if false {
        |left, right| left - right
    } else {
        add
    };
    assert_eq!(unsafe { closure_first(4, 5) }, 9);
}
