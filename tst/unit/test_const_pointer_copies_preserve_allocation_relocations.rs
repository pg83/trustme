static VALUES: [u16; 3] = [73, 79, 83];

const COPIED: (&u16, &u16) = {
    let source = (&VALUES[0], &VALUES[2]);
    let mut target = (&VALUES[1], &VALUES[1]);
    unsafe {
        std::ptr::copy_nonoverlapping(&source, &mut target, 1);
    }
    target
};

fn main() {
    assert_eq!((*COPIED.0, *COPIED.1), (73, 83));
    assert!(std::ptr::eq(COPIED.0, &VALUES[0]));
    assert!(std::ptr::eq(COPIED.1, &VALUES[2]));
}
