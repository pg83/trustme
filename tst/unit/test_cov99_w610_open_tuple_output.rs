fn main() {
    let closure = || -> (_, _) { Default::default() };
    let callable = &closure as *const dyn Fn() -> (u8, u16);
    assert_eq!(unsafe { (*callable)() }, (0, 0));
}
