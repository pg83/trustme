fn pair(recurse: bool) -> impl Copy {
    if recurse {
        let value: (u32, u32) = pair(false);
        return value;
    }
    (1u32, 2u32)
}

fn array() -> [impl Sized; 2] {
    if false {
        let value = array();
        let _: &[i32] = &value;
    }
    [1i32, 2i32]
}

fn main() {
    let _ = pair(true);
    let _ = array();
}
