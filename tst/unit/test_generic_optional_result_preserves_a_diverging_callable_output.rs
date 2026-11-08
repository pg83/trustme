#![feature(unboxed_closures)]

fn invoke<F: FnOnce<()>>(function: F) -> F::Output {
    function()
}

fn never_returns() -> ! {
    loop {}
}

fn select<T>(take_diverging_path: bool) -> Option<T> {
    if take_diverging_path {
        invoke(never_returns)
    } else {
        None
    }
}

fn main() {
    assert_eq!(select::<u16>(false), None);
    assert_eq!(select::<[u8; 3]>(false), None);
}
