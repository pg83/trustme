unsafe extern "C-unwind" { fn abort() -> !; }
struct Notice<'a>(&'a mut [u32; 12]);
impl Drop for Notice<'_> {
    fn drop(&mut self) {
        self.0[0] = 1;
        self.0[1] = 2;
        self.0[2] = 3;
        self.0[3] = 4;
        self.0[4] = 5;
        self.0[5] = 6;
        self.0[6] = 7;
        self.0[7] = 8;
        self.0[8] = 9;
        self.0[9] = 10;
        self.0[10] = 11;
        self.0[11] = 12;
    }
}
#[inline]
fn stop(mark: &mut [u32; 12], target: unsafe extern "C-unwind" fn() -> !) -> ! {
    let _notice = Notice(mark);
    unsafe { target() }
}
fn choose(mark: &mut [u32; 12], halt: bool) -> u32 {
    if halt { stop(mark, abort) }
    let _notice = Notice(mark);
    569
}
fn main() {
    let mut mark = [0; 12];
    assert_eq!(choose(&mut mark, std::hint::black_box(false)), 569);
    assert_eq!(mark, [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]);
}
