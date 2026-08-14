//@ crate-type: lib

async fn outer() -> u32 {
    async fn nested() -> u32 {
        1
    }

    nested().await
}

#[cfg(false)]
fn qualified_syntax() {
    const async unsafe extern "C" fn nested();

    struct Y;
    trait X {
        const async unsafe extern "C" fn f();
    }
    impl X for Y {
        const async unsafe extern "C" fn f();
    }
    impl Y {
        const async unsafe extern "C" fn f();
    }
}
