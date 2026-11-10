//@ compile-fail: Unexpected token TOK_RWORD_CONST

fn main() {
    'guard: const {
        assert!(9u16.wrapping_add(65535) == 8);
    }
}
