use std::ptr::NonNull;

fn main() {
    let mut array = [0u8; 5];
    let _slice: NonNull<[u8]> = NonNull::from(&mut array);
}
