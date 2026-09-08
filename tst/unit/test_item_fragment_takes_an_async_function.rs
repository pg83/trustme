//@ run-pass
// tokio 0.3.7 `task/yield_now.rs`: `cfg_rt! { /// .. #[must_use = ".."] pub async fn
// yield_now() { .. } }` hands an `async fn` to an `$item:item` fragment.  The matcher
// took `unsafe`, `const` and `extern` before `fn` but not `async`, and no arm matched.
macro_rules! cfg_rt {
    ($($item:item)*) => {
        $(
            #[allow(dead_code)]
            $item
        )*
    }
}

cfg_rt! {
    /// Yields execution back to the runtime.
    #[must_use = "yield_now does nothing unless polled/`await`-ed"]
    pub async fn yield_now() {
        /// Yield implementation
        struct YieldNow {
            yielded: bool,
        }
        let _ = YieldNow { yielded: false };
    }

    pub async unsafe fn dangerous() -> u32 {
        7
    }
}

fn main() {
    let _future = yield_now();
    let _other = unsafe { dangerous() };
}
