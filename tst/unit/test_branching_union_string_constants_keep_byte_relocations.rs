//@ compile-variant: -O
#[repr(C)]
#[derive(Copy, Clone)]
union Text { string: &'static str, words: [usize; 2] }
const FIRST: Text = Text { string: "cedar" };
const SECOND: Text = Text { string: "maple" };
#[inline(never)]
fn choose(flag: bool) -> Text {
    let result;
    if flag { result = FIRST; } else { result = SECOND; }
    result
}
fn main() {
    assert_eq!(unsafe { choose(std::hint::black_box(true)).string }, "cedar");
    assert_eq!(unsafe { choose(std::hint::black_box(false)).string }, "maple");
}
