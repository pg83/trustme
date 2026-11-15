//@ env: CXX_x86_64_linux_gnu=c++
//@ compile-flags: --target x86_64-unknown-linux-gnu -C panic=abort

fn multiply_by_six(value: i32) -> i32 {
    value * 6
}

fn main() {
    assert_eq!(multiply_by_six(7), 42);
}
