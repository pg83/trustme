struct Window<T>(T);
impl<T> Window<T> {
    fn address(&mut self) -> *mut T { &mut self.0 }
    fn boxed(self: Box<Self>) -> T { self.0 }
}
fn main() {
    let mut number = Window(71u32);
    let shared: *const u32 = number.address();
    assert_eq!(unsafe { *shared }, 71);
    let unique: *mut u32 = number.address();
    unsafe { *unique += 2; }
    assert_eq!(Box::new(number).boxed(), 73);
}
