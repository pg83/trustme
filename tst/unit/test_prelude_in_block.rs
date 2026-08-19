//@ run-pass
// The prelude a module gets is in scope for everything lexically inside it,
// including the anonymous module that holds the items of a function body. A
// `use` written in a block therefore reaches the prelude's macros the same way
// one written beside the function does.

fn main() {
    {
        use {format_args as f, stringify as s};

        assert_eq!(std::fmt::format(f!("{}", 1)), "1");
        assert_eq!(s!(a + b), "a + b");
    }

    for _ in 0..1 {
        use format_args as g;

        assert_eq!(std::fmt::format(g!("ok")), "ok");
    }
}
