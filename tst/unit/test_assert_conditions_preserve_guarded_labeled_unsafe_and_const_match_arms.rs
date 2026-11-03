fn main() {
    for value in 0u8..=4 {
        assert!(match value {
            #[allow(unreachable_patterns)]
            0 | 1 if value == 0 => true,
            0 | 1 => { true },
            2 => 'chosen: { break 'chosen true },
            3 => unsafe { std::ptr::read(&true) },
            4 => const { true },
            _ => false,
        });
    }
}
