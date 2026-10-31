//@ compile-fail: Invalid trait object vtable
trait Tag {}
static DATA: u8 = 5;
const FORGED: &dyn Tag = unsafe {
    std::mem::transmute::<(*const u8, *const usize), &dyn Tag>((&DATA, &[0usize, 37, 1] as *const [usize; 3] as *const usize))
};
const RESULT: usize = std::mem::size_of_val(FORGED);
fn main() { let _ = RESULT; }
