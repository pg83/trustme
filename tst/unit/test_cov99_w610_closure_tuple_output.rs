fn main() {
    let closure = || -> (_, _) { (23u8, 41u16) };
    let callable = &closure as *const dyn Fn() -> (u8, u16);
    assert_eq!(unsafe { (*callable)() }, (23, 41));
}
