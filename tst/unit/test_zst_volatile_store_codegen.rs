#[derive(Copy, Clone)]
struct Zst;

fn main() {
    // Volatile operations on a ZST have no bytes to access, so this sentinel
    // address must not be dereferenced by generated code.
    unsafe {
        std::ptr::write_volatile(1 as *mut Zst, Zst);
        let _ = std::ptr::read_volatile(1 as *const Zst);
    }
}
