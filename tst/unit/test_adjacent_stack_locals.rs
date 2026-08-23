fn main() {
    for _ in 0..512 {
        let first = 0u64;
        let first_ptr = &first as *const u64;
        let second = 0u64;
        let second_ptr = &second as *const u64;

        if first_ptr.wrapping_add(1) == second_ptr {
            return;
        }
    }

    panic!("never saw adjacent stack locals");
}
