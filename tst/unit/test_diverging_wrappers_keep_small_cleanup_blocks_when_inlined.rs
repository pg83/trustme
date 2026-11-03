unsafe extern "C-unwind" { fn abort() -> !; }
struct Notice<'a>(&'a mut u32);
impl Drop for Notice<'_> { fn drop(&mut self) { *self.0 = 557; } }
#[inline]
fn stop(mark: &mut u32, target: unsafe extern "C-unwind" fn() -> !) -> ! {
    let _notice = Notice(mark);
    unsafe { target() }
}
#[inline]
fn recur(mark: &mut u32) -> ! {
    let mut notice = Notice(mark);
    recur(&mut *notice.0)
}
fn choose(mark: &mut u32, halt: bool, recursive: bool) -> u32 {
    if halt {
        if recursive { recur(mark) } else { stop(mark, abort) }
    }
    let _notice = Notice(mark);
    563
}
fn main() {
    let mut mark = 0;
    assert_eq!(choose(&mut mark, std::hint::black_box(false), false), 563);
    assert_eq!(mark, 557);
}
