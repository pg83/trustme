fn never() -> ! {
    loop {}
}

fn call<F: FnOnce() -> R, R>(function: F) -> R {
    function()
}

fn choose(run: bool) -> u8 {
    if run {
        call(never);
    } else {
        31
    }
}

fn main() {
    assert_eq!(choose(false), 31);
}
