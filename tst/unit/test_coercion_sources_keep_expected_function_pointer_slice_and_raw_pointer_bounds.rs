fn add_seven(x: i32) -> i32 { x + 7 }
fn triple(x: i32) -> i32 { x * 3 }
fn choose(flag: bool) -> fn(i32) -> i32 {
    let callback = match flag { true => add_seven, false => triple };
    callback
}
fn boxed(flag: bool) -> Box<dyn Fn(i32) -> i32> {
    if flag { Box::new(add_seven) } else { Box::new(triple) }
}
fn slice(values: &mut [u16; 3], flag: bool) -> &[u16] {
    if flag { values } else { &*values }
}
fn pointer(left: &mut u16, right: &u16, flag: bool) -> *const u16 {
    if flag { left as *mut u16 } else { right as *const u16 }
}
fn main() {
    assert_eq!(choose(true)(11), 18);
    assert_eq!(choose(false)(11), 33);
    assert_eq!(boxed(true)(13), 20);
    assert_eq!(boxed(false)(13), 39);
    let mut values = [17, 19, 23];
    assert_eq!(slice(&mut values, true), [17, 19, 23]);
    assert_eq!(slice(&mut values, false), [17, 19, 23]);
    let mut left = 29;
    let right = 31;
    unsafe {
        assert_eq!(*pointer(&mut left, &right, true), 29);
        assert_eq!(*pointer(&mut left, &right, false), 31);
    }
}
