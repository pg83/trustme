// Extracted from library/std/src/sync/lazy_lock.rs:32
#![allow(unused)]
fn main() {
    use std::sync::LazyLock;
    
    // Note: static items do not call [`Drop`] on program termination, so this won't be deallocated.
    // this is fine, as the OS can deallocate the terminated program faster than we can free memory
    // but tools like valgrind might report "memory leaks" as it isn't obvious this is intentional.
    static DEEP_THOUGHT: LazyLock<String> = LazyLock::new(|| {
    mod another_crate {
        pub fn great_question() -> String { "42".to_string() }
    }
        // M3 Ultra takes about 16 million years in --release config
        another_crate::great_question()
    });
    
    // The `String` is built, stored in the `LazyLock`, and returned as `&String`.
    let _ = &*DEEP_THOUGHT;
}
