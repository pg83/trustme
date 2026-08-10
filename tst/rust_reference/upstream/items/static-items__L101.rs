// Extracted from src/items/static-items.md:101
#![allow(unused)]
fn main() {
    fn atomic_add(_: *mut u32, _: u32) -> u32 { 2 }
    
    static mut LEVELS: u32 = 0;
    
    // This violates the idea of no shared state, and this doesn't internally
    // protect against races, so this function is `unsafe`
    unsafe fn bump_levels_unsafe() -> u32 {
        unsafe {
            let ret = LEVELS;
            LEVELS += 1;
            return ret;
        }
    }
    
    // As an alternative to `bump_levels_unsafe`, this function is safe, assuming
    // that we have an atomic_add function which returns the old value. This
    // function is safe only if no other code accesses the static in a non-atomic
    // fashion. If such accesses are possible (such as in `bump_levels_unsafe`),
    // then this would need to be `unsafe` to indicate to the caller that they
    // must still guard against concurrent access.
    fn bump_levels_safe() -> u32 {
        unsafe {
            return atomic_add(&raw mut LEVELS, 1);
        }
    }
}
