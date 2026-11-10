//@ compile-fail: Unexpected token TOK_RWORD_UNSAFE

fn main() {
    'guard: unsafe {
        let mut value = 0u16;
        core::ptr::write_volatile(&mut value, 19);
        assert_eq!(value, 19);
    }
}
