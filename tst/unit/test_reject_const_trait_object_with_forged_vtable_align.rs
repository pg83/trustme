//@ compile-fail: Invalid trait object vtable
trait Tag {}
static DATA: u8 = 5;
static HEADER: [usize; 3] = [0, 37, 1];
const FORGED: &dyn Tag = unsafe {
    std::mem::transmute::<(*const u8, *const usize), &dyn Tag>((&DATA, &HEADER[0]))
};
const RESULT: usize = std::mem::align_of_val(FORGED);
fn main() { let _ = RESULT; }
