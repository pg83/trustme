use std::mem::ManuallyDrop;
union Either { left: ManuallyDrop<String>, right: ManuallyDrop<String> }

fn select(left: bool) -> String {
    let value = if left {
        Either { left: ManuallyDrop::new(String::from("left")) }
    } else {
        Either { right: ManuallyDrop::new(String::from("right")) }
    };
    unsafe {
        if left { ManuallyDrop::into_inner(value.left) }
        else { ManuallyDrop::into_inner(value.right) }
    }
}

fn main() {
    assert_eq!(select(std::hint::black_box(true)), "left");
    assert_eq!(select(std::hint::black_box(false)), "right");
}
