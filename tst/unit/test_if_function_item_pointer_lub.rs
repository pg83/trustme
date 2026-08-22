fn fallback(value: u8) -> u8 {
    value + 1
}

fn selected(value: u8) -> u8 {
    value + 2
}

fn call(condition: bool, pointer: *const (), value: u8) -> u8 {
    let function: fn(u8) -> u8 = if condition {
        fallback
    } else {
        unsafe { std::mem::transmute(pointer) }
    };
    function(value)
}

fn main() {
    let pointer = selected as fn(u8) -> u8 as *const ();
    if call(true, pointer, 10) != 11 || call(false, pointer, 10) != 12 {
        std::process::exit(1);
    }
}
