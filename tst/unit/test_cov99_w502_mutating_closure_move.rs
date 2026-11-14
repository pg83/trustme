fn main() {
    let mut value = 0u32;
    let bump = || {
        value += 1;
    };
    let mut moved = bump;
    moved();
    assert_eq!(value, 1);
}
