fn check(which: u8) {
    let flag = true;
    assert!(match which {
        0 => 'selected: { break 'selected flag; }
        1 => unsafe { *(&flag as *const bool) },
        2 => ({ flag }),
        _ => const { true },
    });
}
fn main() {
    for which in 0..4 { check(which); }
}
